/*
 *	Generic address resolution entity
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *	Vitaly E. Lavrov	releasing NULL neighbor in neigh_add.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>

/*
   NOTE. The most unpleasent question is serialization of
   accesses to resolved addresses. The problem is that addresses
   are modified by bh, but they are referenced from normal
   kernel thread. Before today no locking was made.
   My reasoning was that corrupted address token will be copied
   to packet with cosmologically small probability
   (it is even difficult to estimate such small number)
   and it is very silly to waste cycles in fast path to lock them.

   But now I changed my mind, but not because previous statement
   is wrong. Actually, neigh->ha MAY BE not opaque byte array,
   but reference to some private data. In this case even neglibible
   corruption probability becomes bug.

   - hh cache is protected by rwlock. It assumes that
     hh cache update procedure is short and fast, and that
     read_lock is cheaper than start_bh_atomic().
   - ha tokens, saved in neighbour entries, are protected
     by bh_atomic().
   - no protection is made in /proc reading. It is OK, because
     /proc is broken by design in any case, and
     corrupted output is normal behaviour there.

     --ANK (981025)
 */

#define NEIGH_DEBUG 1

#define NEIGH_PRINTK(x...) printk(x)
#define NEIGH_NOPRINTK(x...) do { ; } while(0)
#define NEIGH_PRINTK0 NEIGH_PRINTK
#define NEIGH_PRINTK1 NEIGH_NOPRINTK
#define NEIGH_PRINTK2 NEIGH_NOPRINTK

#if NEIGH_DEBUG >= 1
#undef NEIGH_PRINTK1
#define NEIGH_PRINTK1 NEIGH_PRINTK
#endif
#if NEIGH_DEBUG >= 2
#undef NEIGH_PRINTK2
#define NEIGH_PRINTK2 NEIGH_PRINTK
#endif

static void neigh_timer_handler(unsigned long arg);
#ifdef CONFIG_ARPD
static void neigh_app_notify(struct neighbour *n);
#endif
static int pneigh_ifdown(struct neigh_table *tbl, struct device *dev);

static int neigh_glbl_allocs;
static struct neigh_table *neigh_tables;

static int neigh_blackhole(struct sk_buff *skb)
{
	kfree_skb(skb);
	return -ENETDOWN;
}

/*
 * It is random distribution in the interval (1/2)*base...(3/2)*base.
 * It corresponds to default IPv6 settings and is not overridable,
 * because it is really reasonbale choice.
 */

unsigned long neigh_rand_reach_time(unsigned long base)
{
	return (net_random() % base) + (base>>1);
}


static int neigh_forced_gc(struct neigh_table *tbl)
{
	int shrunk = 0;
	int i;

	if (atomic_read(&tbl->lock))
		return 0;

	for (i=0; i<=NEIGH_HASHMASK; i++) {
		struct neighbour *n, **np;

		np = &tbl->hash_buckets[i];
		while ((n = *np) != NULL) {
			/* Neighbour record may be discarded if:
			   - nobody refers to it.
			   - it is not premanent
			   - (NEW and probably wrong)
			     INCOMPLETE entries are kept at least for
			     n->parms->retrans_time, otherwise we could
			     flood network with resolution requests.
			     It is not clear, what is better table overflow
			     or flooding.
			 */
			if (atomic_read(&n->refcnt) == 0 &&
			    !(n->nud_state&NUD_PERMANENT) &&
			    (n->nud_state != NUD_INCOMPLETE ||
			     jiffies - n->used > n->parms->retrans_time)) {
				*np = n->next;
				n->tbl = NULL;
				tbl->entries--;
				shrunk = 1;
				neigh_destroy(n);
				continue;
			}
			np = &n->next;
		}
	}
	
	tbl->last_flush = jiffies;
	return shrunk;
}

int neigh_ifdown(struct neigh_table *tbl, struct device *dev)
{
	int i;

	if (atomic_read(&tbl->lock)) {
		NEIGH_PRINTK1("neigh_ifdown: impossible event 1763\n");
		return -EBUSY;
	}

	start_bh_atomic();
	for (i=0; i<=NEIGH_HASHMASK; i++) {
		struct neighbour *n, **np;

		np = &tbl->hash_buckets[i];
		while ((n = *np) != NULL) {
			if (dev && n->dev != dev) {
				np = &n->next;
				continue;
			}
			*np = n->next;
			n->tbl = NULL;
			tbl->entries--;
			if (atomic_read(&n->refcnt)) {
				/* The most unpleasant situation.
				   We must destroy neighbour entry,
				   but someone still uses it.

				   The destroy will be delayed until
				   the last user releases us, but
				   we must kill timers etc. and move
				   it to safe state.
				 */
				if (n->nud_state & NUD_IN_TIMER)
					del_timer(&n->timer);
				n->parms = &tbl->parms;
				skb_queue_purge(&n->arp_queue);
				n->output = neigh_blackhole;
				if (n->nud_state&NUD_VALID)
					n->nud_state = NUD_NOARP;
				else
					n->nud_state = NUD_NONE;
				NEIGH_PRINTK2("neigh %p is stray.\n", n);
			} else
				neigh_destroy(n);
		}
	}

	del_timer(&tbl->proxy_timer);
	skb_queue_purge(&tbl->proxy_queue);
	pneigh_ifdown(tbl, dev);
	end_bh_atomic();
	return 0;
}

static struct neighbour *neigh_alloc(struct neigh_table *tbl, int creat)
{
	struct neighbour *n;
	unsigned long now = jiffies;

	if (tbl->entries > tbl->gc_thresh1) {
		if (creat < 0)
			return NULL;
		if (tbl->entries > tbl->gc_thresh3 ||
		    (tbl->entries > tbl->gc_thresh2 &&
		     now - tbl->last_flush > 5*HZ)) {
			if (neigh_forced_gc(tbl) == 0 &&
			    tbl->entries > tbl->gc_thresh3)
				return NULL;
		}
	}

	n = kmalloc(tbl->entry_size, GFP_ATOMIC);
	if (n == NULL)
		return NULL;

	memset(n, 0, tbl->entry_size);

	skb_queue_head_init(&n->arp_queue);
	n->updated = n->used = now;
	n->nud_state = NUD_NONE;
	n->output = neigh_blackhole;
	n->parms = &tbl->parms;
	init_timer(&n->timer);
	n->timer.function = neigh_timer_handler;
	n->timer.data = (unsigned long)n;
	tbl->stats.allocs++;
	neigh_glbl_allocs++;
	return n;
}


struct neighbour * __neigh_lookup(struct neigh_table *tbl, const void *pkey,
				    struct device *dev, int creat)
{
	struct neighbour *n;
	u32 hash_val;
	int key_len = tbl->key_len;

	hash_val = *(u32*)(pkey + key_len - 4);
	hash_val ^= (hash_val>>16);
	hash_val ^= hash_val>>8;
	hash_val ^= hash_val>>3;
	hash_val = (hash_val^dev->ifindex)&NEIGH_HASHMASK;

	for (n = tbl->hash_buckets[hash_val]; n; n = n->next) {
		if (dev == n->dev &&
		    memcmp(n->primary_key, pkey, key_len) == 0) {
			atomic_inc(&n->refcnt);
			return n;
		}
	}
	if (!creat)
		return NULL;

	n = neigh_alloc(tbl, creat);
	if (n == NULL)
		return NULL;

	memcpy(n->primary_key, pkey, key_len);
	n->dev = dev;

	/* Protocol specific setup. */
	if (tbl->constructor &&	tbl->constructor(n) < 0) {
		neigh_destroy(n);
		return NULL;
	}

	/* Device specific setup. */
	if (n->parms && n->parms->neigh_setup && n->parms->neigh_setup(n) < 0) {
		neigh_destroy(n);
		return NULL;
	}

	n->confirmed = jiffies - (n->parms->base_reachable_time<<1);
	atomic_set(&n->refcnt, 1);
	tbl->entries++;
	n->next = tbl->hash_buckets[hash_val];
	tbl->hash_buckets[hash_val] = n;
	n->tbl = tbl;
	NEIGH_PRINTK2("neigh %p is created.\n", n);
	return n;
}

struct pneigh_entry * pneigh_lookup(struct neigh_table *tbl, const void *pkey,
				    struct device *dev, int creat)
{
	struct pneigh_entry *n;
	u32 hash_val;
	int key_len = tbl->key_len;

	hash_val = *(u32*)(pkey + key_len - 4);
	hash_val ^= (hash_val>>16);
	hash_val ^= hash_val>>8;
	hash_val ^= hash_val>>4;
	hash_val &= PNEIGH_HASHMASK;

	for (n = tbl->phash_buckets[hash_val]; n; n = n->next) {
		if (memcmp(n->key, pkey, key_len) == 0 &&
		    (n->dev == dev || !n->dev))
			return n;
	}
	if (!creat)
		return NULL;

	n = kmalloc(sizeof(*n) + key_len, GFP_KERNEL);
	if (n == NULL)
		return NULL;

	memcpy(n->key, pkey, key_len);
	n->dev = dev;

	if (tbl->pconstructor && tbl->pconstructor(n)) {
		kfree(n);
		return NULL;
	}

	n->next = tbl->phash_buckets[hash_val];
	tbl->phash_buckets[hash_val] = n;
	return n;
}


int pneigh_delete(struct neigh_table *tbl, const void *pkey, struct device *dev)
{
	struct pneigh_entry *n, **np;
	u32 hash_val;
	int key_len = tbl->key_len;

	hash_val = *(u32*)(pkey + key_len - 4);
	hash_val ^= (hash_val>>16);
	hash_val ^= hash_val>>8;
	hash_val ^= hash_val>>4;
	hash_val &= PNEIGH_HASHMASK;

	for (np = &tbl->phash_buckets[hash_val]; (n=*np) != NULL; np = &n->next) {
		if (memcmp(n->key, pkey, key_len) == 0 && n->dev == dev) {
			*np = n->next;
			synchronize_bh();
			if (tbl->pdestructor)
				tbl->pdestructor(n);
			kfree(n);
			return 0;
		}
	}
	return -ENOENT;
}

static int pneigh_ifdown(struct neigh_table *tbl, struct device *dev)
{
	struct pneigh_entry *n, **np;
	u32 h;

	for (h=0; h<=PNEIGH_HASHMASK; h++) {
		np = &tbl->phash_buckets[h];
		while ((n=*np) != NULL) {
			if (n->dev == dev || dev == NULL) {
				*np = n->next;
				synchronize_bh();
				if (tbl->pdestructor)
					tbl->pdestructor(n);
				kfree(n);
				continue;
			}
			np = &n->next;
		}
	}
	return -ENOENT;
}


/*
 *	neighbour must already be out of the table;
 *
 */
void neigh_destroy(struct neighbour *neigh)
{	
	struct hh_cache *hh;

	if (neigh->tbl || atomic_read(&neigh->refcnt)) {
		NEIGH_PRINTK1("neigh_destroy: neighbour is use tbl=%p, ref=%d: "
		       "called from %p\n", neigh->tbl, atomic_read(&neigh->refcnt), __builtin_return_address(0));
		return;
	}

	if (neigh->nud_state&NUD_IN_TIMER)
		del_timer(&neigh->timer);

	while ((hh = neigh->hh) != NULL) {
		neigh->hh = hh->hh_next;
		hh->hh_next = NULL;
		hh->hh_output = neigh_blackhole;
		if (atomic_dec_and_test(&hh->hh_refcnt))
			kfree(hh);
	}

	if (neigh->ops && neigh->ops->destructor)
		(neigh->ops->destructor)(neigh);

	skb_queue_purge(&neigh->arp_queue);

	NEIGH_PRINTK2("neigh %p is destroyed.\n", neigh);

	neigh_glbl_allocs--;
	kfree(neigh);
}

/* Neighbour state is suspicious;
   disable fast path.
 */
static void neigh_suspect(struct neighbour *neigh)
{
	struct hh_cache *hh;

	NEIGH_PRINTK2("neigh %p is suspecteded.\n", neigh);

	neigh->output = neigh->ops->output;

	for (hh = neigh->hh; hh; hh = hh->hh_next)
		hh->hh_output = neigh->ops->output;
}

/* Neighbour state is OK;
   enable fast path.
 */
static void neigh_connect(struct neighbour *neigh)
{
	struct hh_cache *hh;

	NEIGH_PRINTK2("neigh %p is connected.\n", neigh);

	neigh->output = neigh->ops->connected_output;

	for (hh = neigh->hh; hh; hh = hh->hh_next)
		hh->hh_output = neigh->ops->hh_output;
}

/*
   Transitions NUD_STALE <-> NUD_REACHABLE do not occur
   when fast path is built: we have no timers assotiated with
   these states, we do not have time to check state when sending.
   neigh_periodic_timer check periodically neigh->confirmed
   time and moves NUD_REACHABLE -> NUD_STALE.

   If a routine wants to know TRUE entry state, it calls
   neigh_sync before checking state.
 */

static void neigh_sync(struct neighbour *n)
{
	unsigned long now = jiffies;
	u8 state = n->nud_state;

	if (state&(NUD_NOARP|NUD_PERMANENT))
		return;
	if (state&NUD_REACHABLE) {
		if (now - n->confirmed > n->parms->reachable_time) {
			n->nud_state = NUD_STALE;
			neigh_suspect(n);
		}
	} else if (state&NUD_VALID) {
		if (now - n->confirmed < n->parms->reachable_time) {
			if (state&NUD_IN_TIMER)
				del_timer(&n->timer);
			n->nud_state = NUD_REACHABLE;
			neigh_connect(n);
		}
	}
}

static void neigh_periodic_timer(unsigned long arg)
{
	struct neigh_table *tbl = (struct neigh_table*)arg;
	unsigned long now = jiffies;
	int i;

	if (atomic_read(&tbl->lock)) {
		tbl->gc_timer.expires = now + 1*HZ;
		add_timer(&tbl->gc_timer);
		return;
	}

	/*
	 *	periodicly recompute ReachableTime from random function
	 */
	
	if (now - tbl->last_rand > 300*HZ) {
		struct neigh_parms *p;
		tbl->last_rand = now;
		for (p=&tbl->parms; p; p = p->next)
			p->reachable_time = neigh_rand_reach_time(p->base_reachable_time);
	}

	for (i=0; i <= NEIGH_HASHMASK; i++) {
		struct neighbour *n, **np;

		np = &tbl->hash_buckets[i];
		while ((n = *np) != NULL) {
			unsigned state = n->nud_state;

			if (state&(NUD_PERMANENT|NUD_IN_TIMER))
				goto next_elt;

			if ((long)(n->used - n->confirmed) < 0)
				n->used = n->confirmed;

			if (atomic_read(&n->refcnt) == 0 &&
			    (state == NUD_FAILED || now - n->used > n->parms->gc_staletime)) {
				*np = n->next;
				n->tbl = NULL;
				n->next = NULL;
				tbl->entries--;
				neigh_destroy(n);
				continue;
			}

			if (n->nud_state&NUD_REACHABLE &&
			    now - n->confirmed > n->parms->reachable_time) {
				n->nud_state = NUD_STALE;
				neigh_suspect(n);
			}

next_elt:
			np = &n->next;
		}
	}

	tbl->gc_timer.expires = now + tbl->gc_interval;
	add_timer(&tbl->gc_timer);
}

static __inline__ int neigh_max_probes(struct neighbour *n)
{
	struct neigh_parms *p = n->parms;
	return p->ucast_probes + p->app_probes + p->mcast_probes;
}


/* Called when a timer expires for a neighbour entry. */

static void neigh_timer_handler(unsigned long arg) 
{
	unsigned long now = jiffies;
	struct neighbour *neigh = (struct neighbour*)arg;
	unsigned state = neigh->nud_state;

	if (!(state&NUD_IN_TIMER)) {
		NEIGH_PRINTK1("neigh: timer & !nud_in_timer\n");
		return;
	}

	if ((state&NUD_VALID) &&
	    now - neigh->confirmed < neigh->parms->reachable_time) {
		neigh->nud_state = NUD_REACHABLE;
		NEIGH_PRINTK2("neigh %p is still alive.\n", neigh);
		neigh_connect(neigh);
		return;
	}
	if (state == NUD_DELAY) {
		NEIGH_PRINTK2("neigh %p is probed.\n", neigh);
		neigh->nud_state = NUD_PROBE;
		neigh->probes = 0;
	}

	if (neigh->probes >= neigh_max_probes(neigh)) {
		struct sk_buff *skb;

		neigh->nud_state = NUD_FAILED;
		neigh->tbl->stats.res_failed++;
		NEIGH_PRINTK2("neigh %p is failed.\n", neigh);

		/* It is very thin place. report_unreachable is very complicated
		   routine. Particularly, it can hit the same neighbour entry!
		   
		   So that, we try to be accurate and avoid dead loop. --ANK
		 */
		while(neigh->nud_state==NUD_FAILED && (skb=__skb_dequeue(&neigh->arp_queue)) != NULL)
			neigh->ops->error_report(neigh, skb);
		skb_queue_purge(&neigh->arp_queue);
		return;
	}

	neigh->timer.expires = now + neigh->parms->retrans_time;
	add_timer(&neigh->timer);

	neigh->ops->solicit(neigh, skb_peek(&neigh->arp_queue));
	neigh->probes++;
}

int __neigh_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	start_bh_atomic();
	if (!(neigh->nud_state&(NUD_CONNECTED|NUD_DELAY|NUD_PROBE))) {
		if (!(neigh->nud_state&(NUD_STALE|NUD_INCOMPLETE))) {
			if (neigh->tbl == NULL) {
				NEIGH_PRINTK2("neigh %p used after death.\n", neigh);
				if (skb)
					kfree_skb(skb);
				end_bh_atomic();
				return 1;
			}
			if (neigh->parms->mcast_probes + neigh->parms->app_probes) {
				neigh->probes = neigh->parms->ucast_probes;
				neigh->nud_state = NUD_INCOMPLETE;
				neigh->timer.expires = jiffies + neigh->parms->retrans_time;
				add_timer(&neigh->timer);

				neigh->ops->solicit(neigh, skb);
				neigh->probes++;
			} else {
				neigh->nud_state = NUD_FAILED;
				if (skb)
					kfree_skb(skb);
				end_bh_atomic();
				return 1;
			}
		}
		if (neigh->nud_state == NUD_INCOMPLETE) {
			if (skb) {
				if (skb_queue_len(&neigh->arp_queue) >= neigh->parms->queue_len) {
					struct sk_buff *buff;
					buff = neigh->arp_queue.prev;
					__skb_unlink(buff, &neigh->arp_queue);
					kfree_skb(buff);
				}
				__skb_queue_head(&neigh->arp_queue, skb);
			}
			end_bh_atomic();
			return 1;
		}
		if (neigh->nud_state == NUD_STALE) {
			NEIGH_PRINTK2("neigh %p is delayed.\n", neigh);
			neigh->nud_state = NUD_DELAY;
			neigh->timer.expires = jiffies + neigh->parms->delay_probe_time;
			add_timer(&neigh->timer);
		}
	}
	end_bh_atomic();
	return 0;
}

static __inline__ void neigh_update_hhs(struct neighbour *neigh)
{
	struct hh_cache *hh;
	void (*update)(struct hh_cache*, struct device*, unsigned char*) =
		neigh->dev->header_cache_update;

	if (update) {
		for (hh=neigh->hh; hh; hh=hh->hh_next) {
			write_lock_irq(&hh->hh_lock);
			update(hh, neigh->dev, neigh->ha);
			write_unlock_irq(&hh->hh_lock);
		}
	}
}



/* Generic update routine.
   -- lladdr is new lladdr or NULL, if it is not supplied.
   -- new    is new state.
   -- override==1 allows to override existing lladdr, if it is different.
   -- arp==0 means that the change is administrative.
 */

int neigh_update(struct neighbour *neigh, u8 *lladdr, u8 new, int override, int arp)
{
	u8 old = neigh->nud_state;
	struct device *dev = neigh->dev;

	if (arp && (old&(NUD_NOARP|NUD_PERMANENT)))
		return -EPERM;

	if (!(new&NUD_VALID)) {
		if (old&NUD_IN_TIMER)
			del_timer(&neigh->timer);
		if (old&NUD_CONNECTED)
			neigh_suspect(neigh);
		neigh->nud_state = new;
		return 0;
	}

	/* Compare new lladdr with cached one */
	if (dev->addr_len == 0) {
		/* First case: device needs no address. */
		lladdr = neigh->ha;
	} else if (lladdr) {
		/* The second case: if something is already cached
		   and a new address is proposed:
		   - compare new & old
		   - if they are different, check override flag
		 */
		if (old&NUD_VALID) {
			if (memcmp(lladdr, neigh->ha, dev->addr_len) == 0)
				lladdr = neigh->ha;
			else if (!override)
				return -EPERM;
		}
	} else {
		/* No address is supplied; if we know something,
		   use it, otherwise discard the request.
		 */
		if (!(old&NUD_VALID))
			return -EINVAL;
		lladdr = neigh->ha;
	}

	neigh_sync(neigh);
	old = neigh->nud_state;
	if (new&NUD_CONNECTED)
		neigh->confirmed = jiffies;
	neigh->updated = jiffies;

	/* If entry was valid and address is not changed,
	   do not change entry state, if new one is STALE.
	 */
	if (old&NUD_VALID) {
		if (lladdr == neigh->ha)
			if (new == old || (new == NUD_STALE && (old&NUD_CONNECTED)))
				return 0;
	}
	if (old&NUD_IN_TIMER)
		del_timer(&neigh->timer);
	neigh->nud_state = new;
	if (lladdr != neigh->ha) {
		memcpy(&neigh->ha, lladdr, dev->addr_len);
		neigh_update_hhs(neigh);
		neigh->confirmed = jiffies - (neigh->parms->base_reachable_time<<1);
#ifdef CONFIG_ARPD
		if (neigh->parms->app_probes)
			neigh_app_notify(neigh);
#endif
	}
	if (new == old)
		return 0;
	if (new&NUD_CONNECTED)
		neigh_connect(neigh);
	else
		neigh_suspect(neigh);
	if (!(old&NUD_VALID)) {
		struct sk_buff *skb;

		/* Again: avoid dead loop if something went wrong */

		while (neigh->nud_state&NUD_VALID &&
		       (skb=__skb_dequeue(&neigh->arp_queue)) != NULL) {
			struct neighbour *n1 = neigh;
			/* On shaper/eql skb->dst->neighbour != neigh :( */
			if (skb->dst && skb->dst->neighbour)
				n1 = skb->dst->neighbour;
			n1->output(skb);
		}
		skb_queue_purge(&neigh->arp_queue);
	}
	return 0;
}

struct neighbour * neigh_event_ns(struct neigh_table *tbl,
				  u8 *lladdr, void *saddr,
				  struct device *dev)
{
	struct neighbour *neigh;

	neigh = __neigh_lookup(tbl, saddr, dev, lladdr || !dev->addr_len);
	if (neigh)
		neigh_update(neigh, lladdr, NUD_STALE, 1, 1);
	return neigh;
}

static void neigh_hh_init(struct neighbour *n, struct dst_entry *dst, u16 protocol)
{
	struct hh_cache	*hh = NULL;
	struct device *dev = dst->dev;

	for (hh=n->hh; hh; hh = hh->hh_next)
		if (hh->hh_type == protocol)
			break;

	if (!hh && (hh = kmalloc(sizeof(*hh), GFP_ATOMIC)) != NULL) {
		memset(hh, 0, sizeof(struct hh_cache));
		hh->hh_type = protocol;
		atomic_set(&hh->hh_refcnt, 0);
		hh->hh_next = NULL;
		if (dev->hard_header_cache(n, hh)) {
			kfree(hh);
			hh = NULL;
		} else {
			atomic_inc(&hh->hh_refcnt);
			hh->hh_next = n->hh;
			n->hh = hh;
			if (n->nud_state&NUD_CONNECTED)
				hh->hh_output = n->ops->hh_output;
			else
				hh->hh_output = n->ops->output;
		}
	}
	if (hh)	{
		atomic_inc(&hh->hh_refcnt);
		dst->hh = hh;
	}
}

/* This function can be used in contexts, where only old dev_queue_xmit
   worked, f.e. if you want to override normal output path (eql, shaper),
   but resoltution is not made yet.
 */

int neigh_compat_output(struct sk_buff *skb)
{
	struct device *dev = skb->dev;

	__skb_pull(skb, skb->nh.raw - skb->data);

	if (dev->hard_header &&
	    dev->hard_header(skb, dev, ntohs(skb->protocol), NULL, NULL, skb->len) < 0 &&
	    dev->rebuild_header(skb))
		return 0;

	return dev_queue_xmit(skb);
}

/* Slow and careful. */

int neigh_resolve_output(struct sk_buff *skb)
{
	struct dst_entry *dst = skb->dst;
	struct neighbour *neigh;

	if (!dst || !(neigh = dst->neighbour))
		goto discard;

	__skb_pull(skb, skb->nh.raw - skb->data);

	if (neigh_event_send(neigh, skb) == 0) {
		int err;
		struct device *dev = neigh->dev;
		if (dev->hard_header_cache && dst->hh == NULL) {
			start_bh_atomic();
			if (dst->hh == NULL)
				neigh_hh_init(neigh, dst, dst->ops->protocol);
			err = dev->hard_header(skb, dev, ntohs(skb->protocol), neigh->ha, NULL, skb->len);
			end_bh_atomic();
		} else {
			start_bh_atomic();
			err = dev->hard_header(skb, dev, ntohs(skb->protocol), neigh->ha, NULL, skb->len);
			end_bh_atomic();
		}
		if (err >= 0)
			return neigh->ops->queue_xmit(skb);
		kfree_skb(skb);
		return -EINVAL;
	}
	return 0;

discard:
	NEIGH_PRINTK1("neigh_resolve_output: dst=%p neigh=%p\n", dst, dst ? dst->neighbour : NULL);
	kfree_skb(skb);
	return -EINVAL;
}

/* As fast as possible without hh cache */

int neigh_connected_output(struct sk_buff *skb)
{
	int err;
	struct dst_entry *dst = skb->dst;
	struct neighbour *neigh = dst->neighbour;
	struct device *dev = neigh->dev;

	__skb_pull(skb, skb->nh.raw - skb->data);

	start_bh_atomic();
	err = dev->hard_header(skb, dev, ntohs(skb->protocol), neigh->ha, NULL, skb->len);
	end_bh_atomic();
	if (err >= 0)
		return neigh->ops->queue_xmit(skb);
	kfree_skb(skb);
	return -EINVAL;
}

static void neigh_proxy_process(unsigned long arg)
{
	struct neigh_table *tbl = (struct neigh_table *)arg;
	long sched_next = 0;
	unsigned long now = jiffies;
	struct sk_buff *skb = tbl->proxy_queue.next;

	while (skb != (struct sk_buff*)&tbl->proxy_queue) {
		struct sk_buff *back = skb;
		long tdif = back->stamp.tv_usec - now;

		skb = skb->next;
		if (tdif <= 0) {
			__skb_unlink(back, &tbl->proxy_queue);
			if (tbl->proxy_redo)
				tbl->proxy_redo(back);
			else
				kfree_skb(back);
		} else if (!sched_next || tdif < sched_next)
			sched_next = tdif;
	}
	del_timer(&tbl->proxy_timer);
	if (sched_next) {
		tbl->proxy_timer.expires = jiffies + sched_next;
		add_timer(&tbl->proxy_timer);
	}
}

void pneigh_enqueue(struct neigh_table *tbl, struct neigh_parms *p,
		    struct sk_buff *skb)
{
	unsigned long now = jiffies;
	long sched_next = net_random()%p->proxy_delay;

	if (tbl->proxy_queue.qlen > p->proxy_qlen) {
		kfree_skb(skb);
		return;
	}
	skb->stamp.tv_sec = 0;
	skb->stamp.tv_usec = now + sched_next;
	if (del_timer(&tbl->proxy_timer)) {
		long tval = tbl->proxy_timer.expires - now;
		if (tval < sched_next)
			sched_next = tval;
	}
	tbl->proxy_timer.expires = now + sched_next;
	dst_release(skb->dst);
	skb->dst = NULL;
	__skb_queue_tail(&tbl->proxy_queue, skb);
	add_timer(&tbl->proxy_timer);
}


struct neigh_parms *neigh_parms_alloc(struct device *dev, struct neigh_table *tbl)
{
	struct neigh_parms *p;
	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p) {
		memcpy(p, &tbl->parms, sizeof(*p));
		p->tbl = tbl;
		p->reachable_time = neigh_rand_reach_time(p->base_reachable_time);
		if (dev && dev->neigh_setup) {
			if (dev->neigh_setup(dev, p)) {
				kfree(p);
				return NULL;
			}
		}
		p->next = tbl->parms.next;
		tbl->parms.next = p;
	}
	return p;
}

void neigh_parms_release(struct neigh_table *tbl, struct neigh_parms *parms)
{
	struct neigh_parms **p;
	
	if (parms == NULL || parms == &tbl->parms)
		return;
	for (p = &tbl->parms.next; *p; p = &(*p)->next) {
		if (*p == parms) {
			*p = parms->next;
			synchronize_bh();
#ifdef CONFIG_SYSCTL
			neigh_sysctl_unregister(parms);
#endif
			kfree(parms);
			return;
		}
	}
	NEIGH_PRINTK1("neigh_release_parms: not found\n");
}


void neigh_table_init(struct neigh_table *tbl)
{
	unsigned long now = jiffies;

	tbl->parms.reachable_time = neigh_rand_reach_time(tbl->parms.base_reachable_time);

	init_timer(&tbl->gc_timer);
	tbl->gc_timer.data = (unsigned long)tbl;
	tbl->gc_timer.function = neigh_periodic_timer;
	tbl->gc_timer.expires = now + tbl->gc_interval + tbl->parms.reachable_time;
	add_timer(&tbl->gc_timer);

	init_timer(&tbl->proxy_timer);
	tbl->proxy_timer.data = (unsigned long)tbl;
	tbl->proxy_timer.function = neigh_proxy_process;
	skb_queue_head_init(&tbl->proxy_queue);

	tbl->last_flush = now;
	tbl->last_rand = now + tbl->parms.reachable_time*20;
	tbl->next = neigh_tables;
	neigh_tables = tbl;
}

int neigh_table_clear(struct neigh_table *tbl)
{
	struct neigh_table **tp;

	start_bh_atomic();
	del_timer(&tbl->gc_timer);
	del_timer(&tbl->proxy_timer);
	skb_queue_purge(&tbl->proxy_queue);
	neigh_ifdown(tbl, NULL);
	end_bh_atomic();
	if (tbl->entries)
		printk(KERN_CRIT "neighbour leakage\n");
	for (tp = &neigh_tables; *tp; tp = &(*tp)->next) {
		if (*tp == tbl) {
			*tp = tbl->next;
			synchronize_bh();
			break;
		}
	}
#ifdef CONFIG_SYSCTL
	neigh_sysctl_unregister(&tbl->parms);
#endif
	return 0;
}

#ifdef CONFIG_RTNETLINK


int neigh_delete(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct ndmsg *ndm = NLMSG_DATA(nlh);
	struct rtattr **nda = arg;
	struct neigh_table *tbl;
	struct device *dev = NULL;

	if (ndm->ndm_ifindex) {
		if ((dev = dev_get_by_index(ndm->ndm_ifindex)) == NULL)
			return -ENODEV;
	}

	for (tbl=neigh_tables; tbl; tbl = tbl->next) {
		int err = 0;
		struct neighbour *n;

		if (tbl->family != ndm->ndm_family)
			continue;

		if (nda[NDA_DST-1] == NULL ||
		    nda[NDA_DST-1]->rta_len != RTA_LENGTH(tbl->key_len))
			return -EINVAL;

		if (ndm->ndm_flags&NTF_PROXY)
			return pneigh_delete(tbl, RTA_DATA(nda[NDA_DST-1]), dev);

		if (dev == NULL)
			return -EINVAL;

		start_bh_atomic();
		n = __neigh_lookup(tbl, RTA_DATA(nda[NDA_DST-1]), dev, 0);
		if (n) {
			err = neigh_update(n, NULL, NUD_FAILED, 1, 0);
			neigh_release(n);
		}
		end_bh_atomic();
		return err;
	}

	return -EADDRNOTAVAIL;
}

int neigh_add(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct ndmsg *ndm = NLMSG_DATA(nlh);
	struct rtattr **nda = arg;
	struct neigh_table *tbl;
	struct device *dev = NULL;

	if (ndm->ndm_ifindex) {
		if ((dev = dev_get_by_index(ndm->ndm_ifindex)) == NULL)
			return -ENODEV;
	}

	for (tbl=neigh_tables; tbl; tbl = tbl->next) {
		int err = 0;
		struct neighbour *n;

		if (tbl->family != ndm->ndm_family)
			continue;
		if (nda[NDA_DST-1] == NULL ||
		    nda[NDA_DST-1]->rta_len != RTA_LENGTH(tbl->key_len))
			return -EINVAL;
		if (ndm->ndm_flags&NTF_PROXY) {
			if (pneigh_lookup(tbl, RTA_DATA(nda[NDA_DST-1]), dev, 1))
				return 0;
			return -ENOBUFS;
		}
		if (dev == NULL)
			return -EINVAL;
		if (nda[NDA_LLADDR-1] != NULL &&
		    nda[NDA_LLADDR-1]->rta_len != RTA_LENGTH(dev->addr_len))
			return -EINVAL;
		start_bh_atomic();
		n = __neigh_lookup(tbl, RTA_DATA(nda[NDA_DST-1]), dev, 0);
		if (n) {
			if (nlh->nlmsg_flags&NLM_F_EXCL)
				err = -EEXIST;
		} else if (!(nlh->nlmsg_flags&NLM_F_CREATE))
			err = -ENOENT;
		else {
			n = __neigh_lookup(tbl, RTA_DATA(nda[NDA_DST-1]), dev, 1);
			if (n == NULL)
				err = -ENOBUFS;
		}
		if (err == 0) {
			err = neigh_update(n, nda[NDA_LLADDR-1] ? RTA_DATA(nda[NDA_LLADDR-1]) : NULL,
					   ndm->ndm_state,
					   nlh->nlmsg_flags&NLM_F_REPLACE, 0);
		}
		if (n)
			neigh_release(n);
		end_bh_atomic();
		return err;
	}

	return -EADDRNOTAVAIL;
}


static int neigh_fill_info(struct sk_buff *skb, struct neighbour *n,
			   u32 pid, u32 seq, int event)
{
	unsigned long now = jiffies;
	struct ndmsg *ndm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;
	struct nda_cacheinfo ci;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*ndm));
	ndm = NLMSG_DATA(nlh);
	ndm->ndm_family = n->ops->family;
	ndm->ndm_flags = n->flags;
	ndm->ndm_type = n->type;
	ndm->ndm_state = n->nud_state;
	ndm->ndm_ifindex = n->dev->ifindex;
	RTA_PUT(skb, NDA_DST, n->tbl->key_len, n->primary_key);
	if (n->nud_state&NUD_VALID)
		RTA_PUT(skb, NDA_LLADDR, n->dev->addr_len, n->ha);
	ci.ndm_used = now - n->used;
	ci.ndm_confirmed = now - n->confirmed;
	ci.ndm_updated = now - n->updated;
	ci.ndm_refcnt = atomic_read(&n->refcnt);
	RTA_PUT(skb, NDA_CACHEINFO, sizeof(ci), &ci);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}


static int neigh_dump_table(struct neigh_table *tbl, struct sk_buff *skb, struct netlink_callback *cb)
{
	struct neighbour *n;
	int h, s_h;
	int idx, s_idx;

	s_h = cb->args[1];
	s_idx = idx = cb->args[2];
	for (h=0; h <= NEIGH_HASHMASK; h++) {
		if (h < s_h) continue;
		if (h > s_h)
			s_idx = 0;
		start_bh_atomic();
		for (n = tbl->hash_buckets[h], idx = 0; n;
		     n = n->next, idx++) {
			if (idx < s_idx)
				continue;
			if (neigh_fill_info(skb, n, NETLINK_CB(cb->skb).pid,
					    cb->nlh->nlmsg_seq, RTM_NEWNEIGH) <= 0) {
				end_bh_atomic();
				cb->args[1] = h;
				cb->args[2] = idx;
				return -1;
			}
		}
		end_bh_atomic();
	}

	cb->args[1] = h;
	cb->args[2] = idx;
	return skb->len;
}

int neigh_dump_info(struct sk_buff *skb, struct netlink_callback *cb)
{
	int t;
	int s_t;
	struct neigh_table *tbl;
	int family = ((struct rtgenmsg*)NLMSG_DATA(cb->nlh))->rtgen_family;

	s_t = cb->args[0];

	for (tbl=neigh_tables, t=0; tbl; tbl = tbl->next, t++) {
		if (t < s_t) continue;
		if (family && tbl->family != family)
			continue;
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(cb->args[0]));
		if (neigh_dump_table(tbl, skb, cb) < 0) 
			break;
	}

	cb->args[0] = t;

	return skb->len;
}

#ifdef CONFIG_ARPD
void neigh_app_ns(struct neighbour *n)
{
	struct sk_buff *skb;
	struct nlmsghdr  *nlh;
	int size = NLMSG_SPACE(sizeof(struct ndmsg)+256);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return;

	if (neigh_fill_info(skb, n, 0, 0, RTM_GETNEIGH) < 0) {
		kfree_skb(skb);
		return;
	}
	nlh = (struct nlmsghdr*)skb->data;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	NETLINK_CB(skb).dst_groups = RTMGRP_NEIGH;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_NEIGH, GFP_ATOMIC);
}

static void neigh_app_notify(struct neighbour *n)
{
	struct sk_buff *skb;
	struct nlmsghdr  *nlh;
	int size = NLMSG_SPACE(sizeof(struct ndmsg)+256);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return;

	if (neigh_fill_info(skb, n, 0, 0, RTM_NEWNEIGH) < 0) {
		kfree_skb(skb);
		return;
	}
	nlh = (struct nlmsghdr*)skb->data;
	NETLINK_CB(skb).dst_groups = RTMGRP_NEIGH;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_NEIGH, GFP_ATOMIC);
}



#endif


#endif

#ifdef CONFIG_SYSCTL

struct neigh_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table neigh_vars[17];
	ctl_table neigh_dev[2];
	ctl_table neigh_neigh_dir[2];
	ctl_table neigh_proto_dir[2];
	ctl_table neigh_root_dir[2];
} neigh_sysctl_template = {
	NULL,
        {{NET_NEIGH_MCAST_SOLICIT, "mcast_solicit",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_UCAST_SOLICIT, "ucast_solicit",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_APP_SOLICIT, "app_solicit",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_RETRANS_TIME, "retrans_time",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_REACHABLE_TIME, "base_reachable_time",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_NEIGH_DELAY_PROBE_TIME, "delay_first_probe_time",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_NEIGH_GC_STALE_TIME, "gc_stale_time",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_NEIGH_UNRES_QLEN, "unres_qlen",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_PROXY_QLEN, "proxy_qlen",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_ANYCAST_DELAY, "anycast_delay",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_PROXY_DELAY, "proxy_delay",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_LOCKTIME, "locktime",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_GC_INTERVAL, "gc_interval",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},
	{NET_NEIGH_GC_THRESH1, "gc_thresh1",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_GC_THRESH2, "gc_thresh2",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_NEIGH_GC_THRESH3, "gc_thresh3",
         NULL, sizeof(int), 0644, NULL,
         &proc_dointvec},
	 {0}},

	{{NET_PROTO_CONF_DEFAULT, "default", NULL, 0, 0555, NULL},{0}},
	{{0, "neigh", NULL, 0, 0555, NULL},{0}},
	{{0, NULL, NULL, 0, 0555, NULL},{0}},
	{{CTL_NET, "net", NULL, 0, 0555, NULL},{0}}
};

int neigh_sysctl_register(struct device *dev, struct neigh_parms *p,
			  int p_id, int pdev_id, char *p_name)
{
	struct neigh_sysctl_table *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return -ENOBUFS;
	memcpy(t, &neigh_sysctl_template, sizeof(*t));
	t->neigh_vars[0].data = &p->mcast_probes;
	t->neigh_vars[1].data = &p->ucast_probes;
	t->neigh_vars[2].data = &p->app_probes;
	t->neigh_vars[3].data = &p->retrans_time;
	t->neigh_vars[4].data = &p->base_reachable_time;
	t->neigh_vars[5].data = &p->delay_probe_time;
	t->neigh_vars[6].data = &p->gc_staletime;
	t->neigh_vars[7].data = &p->queue_len;
	t->neigh_vars[8].data = &p->proxy_qlen;
	t->neigh_vars[9].data = &p->anycast_delay;
	t->neigh_vars[10].data = &p->proxy_delay;
	t->neigh_vars[11].data = &p->locktime;
	if (dev) {
		t->neigh_dev[0].procname = dev->name;
		t->neigh_dev[0].ctl_name = dev->ifindex;
		memset(&t->neigh_vars[12], 0, sizeof(ctl_table));
	} else {
		t->neigh_vars[12].data = (int*)(p+1);
		t->neigh_vars[13].data = (int*)(p+1) + 1;
		t->neigh_vars[14].data = (int*)(p+1) + 2;
		t->neigh_vars[15].data = (int*)(p+1) + 3;
	}
	t->neigh_neigh_dir[0].ctl_name = pdev_id;

	t->neigh_proto_dir[0].procname = p_name;
	t->neigh_proto_dir[0].ctl_name = p_id;

	t->neigh_dev[0].child = t->neigh_vars;
	t->neigh_neigh_dir[0].child = t->neigh_dev;
	t->neigh_proto_dir[0].child = t->neigh_neigh_dir;
	t->neigh_root_dir[0].child = t->neigh_proto_dir;

	t->sysctl_header = register_sysctl_table(t->neigh_root_dir, 0);
	if (t->sysctl_header == NULL) {
		kfree(t);
		return -ENOBUFS;
	}
	p->sysctl_table = t;
	return 0;
}

void neigh_sysctl_unregister(struct neigh_parms *p)
{
	if (p->sysctl_table) {
		struct neigh_sysctl_table *t = p->sysctl_table;
		p->sysctl_table = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t);
	}
}

#endif	/* CONFIG_SYSCTL */
