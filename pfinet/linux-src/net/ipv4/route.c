/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		ROUTE - implementation of the IP router.
 *
 * Version:	$Id: route.c,v 1.67.2.4 1999/11/16 02:28:43 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Linus Torvalds, <Linus.Torvalds@helsinki.fi>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *		Alan Cox	:	Verify area fixes.
 *		Alan Cox	:	cli() protects routing changes
 *		Rui Oliveira	:	ICMP routing table updates
 *		(rco@di.uminho.pt)	Routing table insertion and update
 *		Linus Torvalds	:	Rewrote bits to be sensible
 *		Alan Cox	:	Added BSD route gw semantics
 *		Alan Cox	:	Super /proc >4K 
 *		Alan Cox	:	MTU in route table
 *		Alan Cox	: 	MSS actually. Also added the window
 *					clamper.
 *		Sam Lantinga	:	Fixed route matching in rt_del()
 *		Alan Cox	:	Routing cache support.
 *		Alan Cox	:	Removed compatibility cruft.
 *		Alan Cox	:	RTF_REJECT support.
 *		Alan Cox	:	TCP irtt support.
 *		Jonathan Naylor	:	Added Metric support.
 *	Miquel van Smoorenburg	:	BSD API fixes.
 *	Miquel van Smoorenburg	:	Metrics.
 *		Alan Cox	:	Use __u32 properly
 *		Alan Cox	:	Aligned routing errors more closely with BSD
 *					our system is still very different.
 *		Alan Cox	:	Faster /proc handling
 *	Alexey Kuznetsov	:	Massive rework to support tree based routing,
 *					routing caches and better behaviour.
 *		
 *		Olaf Erb	:	irtt wasn't being copied right.
 *		Bjorn Ekwall	:	Kerneld route support.
 *		Alan Cox	:	Multicast fixed (I hope)
 * 		Pavel Krauz	:	Limited broadcast fixed
 *		Mike McLagan	:	Routing by source
 *	Alexey Kuznetsov	:	End of old history. Splitted to fib.c and
 *					route.c and rewritten from scratch.
 *		Andi Kleen	:	Load-limit warning messages.
 *	Vitaly E. Lavrov	:	Transparent proxy revived after year coma.
 *	Vitaly E. Lavrov	:	Race condition in ip_route_input_slow.
 *	Tobias Ringstrom	:	Uninitialized res.type in ip_route_output_slow.
 *	Vladimir V. Ivanov	:	IP rule info (flowid) is really useful.
 *		Marc Boucher	:	routing by fwmark
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/pkt_sched.h>
#include <linux/mroute.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/arp.h>
#include <net/tcp.h>
#include <net/icmp.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#define IP_MAX_MTU	0xFFF0

#define RT_GC_TIMEOUT (300*HZ)

int ip_rt_min_delay = 2*HZ;
int ip_rt_max_delay = 10*HZ;
int ip_rt_gc_thresh = RT_HASH_DIVISOR;
int ip_rt_max_size = RT_HASH_DIVISOR*16;
int ip_rt_gc_timeout = RT_GC_TIMEOUT;
int ip_rt_gc_interval = 60*HZ;
int ip_rt_gc_min_interval = 5*HZ;
int ip_rt_redirect_number = 9;
int ip_rt_redirect_load = HZ/50;
int ip_rt_redirect_silence = ((HZ/50) << (9+1));
int ip_rt_error_cost = HZ;
int ip_rt_error_burst = 5*HZ;
int ip_rt_gc_elasticity = 8;
int ip_rt_mtu_expires = 10*60*HZ;

static unsigned long rt_deadline = 0;

#define RTprint(a...)	printk(KERN_DEBUG a)

static void rt_run_flush(unsigned long dummy);

static struct timer_list rt_flush_timer =
	{ NULL, NULL, 0, 0L, rt_run_flush };
static struct timer_list rt_periodic_timer =
	{ NULL, NULL, 0, 0L, NULL };

/*
 *	Interface to generic destination cache.
 */

static struct dst_entry * ipv4_dst_check(struct dst_entry * dst, u32);
static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst,
					   struct sk_buff *);
static struct dst_entry * ipv4_negative_advice(struct dst_entry *);
static void		  ipv4_link_failure(struct sk_buff *skb);
static int rt_garbage_collect(void);


struct dst_ops ipv4_dst_ops =
{
	AF_INET,
	__constant_htons(ETH_P_IP),
	RT_HASH_DIVISOR,

	rt_garbage_collect,
	ipv4_dst_check,
	ipv4_dst_reroute,
	NULL,
	ipv4_negative_advice,
	ipv4_link_failure,
};

__u8 ip_tos2prio[16] = {
	TC_PRIO_BESTEFFORT,
	TC_PRIO_FILLER,
	TC_PRIO_BESTEFFORT,
	TC_PRIO_FILLER,
	TC_PRIO_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE_BULK,
	TC_PRIO_FILLER,
	TC_PRIO_INTERACTIVE_BULK,
	TC_PRIO_FILLER
};


/*
 * Route cache.
 */

struct rtable 	*rt_hash_table[RT_HASH_DIVISOR];

static int rt_intern_hash(unsigned hash, struct rtable * rth, struct rtable ** res);

static __inline__ unsigned rt_hash_code(u32 daddr, u32 saddr, u8 tos)
{
	unsigned hash = ((daddr&0xF0F0F0F0)>>4)|((daddr&0x0F0F0F0F)<<4);
	hash = hash^saddr^tos;
	hash = hash^(hash>>16);
	return (hash^(hash>>8)) & 0xFF;
}

#ifdef CONFIG_PROC_FS

static int rt_cache_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t pos=0;
	char temp[129];
	struct rtable *r;
	int i;

	pos = 128;

	if (offset<128)	{
		sprintf(buffer,"%-127s\n", "Iface\tDestination\tGateway \tFlags\t\tRefCnt\tUse\tMetric\tSource\t\tMTU\tWindow\tIRTT\tTOS\tHHRef\tHHUptod\tSpecDst");
		len = 128;
  	}
	
  	
	start_bh_atomic();

	for (i = 0; i<RT_HASH_DIVISOR; i++) {
		for (r = rt_hash_table[i]; r; r = r->u.rt_next) {
			/*
			 *	Spin through entries until we are ready
			 */
			pos += 128;

			if (pos <= offset) {
				len = 0;
				continue;
			}
			sprintf(temp, "%s\t%08lX\t%08lX\t%8X\t%d\t%u\t%d\t%08lX\t%d\t%u\t%u\t%02X\t%d\t%1d\t%08X",
				r->u.dst.dev ? r->u.dst.dev->name : "*",
				(unsigned long)r->rt_dst,
				(unsigned long)r->rt_gateway,
				r->rt_flags,
				atomic_read(&r->u.dst.use),
				atomic_read(&r->u.dst.refcnt),
				0,
				(unsigned long)r->rt_src, (int)r->u.dst.pmtu,
				r->u.dst.window,
				(int)r->u.dst.rtt, r->key.tos,
				r->u.dst.hh ? atomic_read(&r->u.dst.hh->hh_refcnt) : -1,
				r->u.dst.hh ? (r->u.dst.hh->hh_output == dev_queue_xmit) : 0,
				r->rt_spec_dst);
			sprintf(buffer+len,"%-127s\n",temp);
			len += 128;
			if (pos >= offset+length)
				goto done;
		}
        }

done:
	end_bh_atomic();
  	
  	*start = buffer+len-(pos-offset);
  	len = pos-offset;
  	if (len>length)
  		len = length;
  	return len;
}
#endif
  
static __inline__ void rt_free(struct rtable *rt)
{
	dst_free(&rt->u.dst);
}

static __inline__ void rt_drop(struct rtable *rt)
{
	ip_rt_put(rt);
	dst_free(&rt->u.dst);
}

static __inline__ int rt_fast_clean(struct rtable *rth)
{
	/* Kill broadcast/multicast entries very aggresively, if they
	   collide in hash table with more useful entries */
	return ((rth->rt_flags&(RTCF_BROADCAST|RTCF_MULTICAST))
		&& rth->key.iif && rth->u.rt_next);
}

static __inline__ int rt_valuable(struct rtable *rth)
{
	return ((rth->rt_flags&(RTCF_REDIRECTED|RTCF_NOTIFY))
		|| rth->u.dst.expires);
}

static __inline__ int rt_may_expire(struct rtable *rth, int tmo1, int tmo2)
{
	int age;

	if (atomic_read(&rth->u.dst.use))
		return 0;

	if (rth->u.dst.expires && (long)(rth->u.dst.expires - jiffies) <= 0)
		return 1;

	age = jiffies - rth->u.dst.lastuse;
	if (age <= tmo1 && !rt_fast_clean(rth))
		return 0;
	if (age <= tmo2 && rt_valuable(rth))
		return 0;
	return 1;
}

static void rt_check_expire(unsigned long dummy)
{
	int i;
	static int rover;
	struct rtable *rth, **rthp;
	unsigned long now = jiffies;

	for (i=0; i<RT_HASH_DIVISOR/5; i++) {
		unsigned tmo = ip_rt_gc_timeout;

		rover = (rover + 1) & (RT_HASH_DIVISOR-1);
		rthp = &rt_hash_table[rover];

		while ((rth = *rthp) != NULL) {
			if (rth->u.dst.expires) {
				/* Entrie is expired even if it is in use */
				if ((long)(now - rth->u.dst.expires) <= 0) {
					tmo >>= 1;
					rthp = &rth->u.rt_next;
					continue;
				}
			} else if (!rt_may_expire(rth, tmo, ip_rt_gc_timeout)) {
				tmo >>= 1;
				rthp = &rth->u.rt_next;
				continue;
			}

			/*
			 * Cleanup aged off entries.
			 */
			*rthp = rth->u.rt_next;
			rt_free(rth);
		}

		/* Fallback loop breaker. */
		if ((jiffies - now) > 0)
			break;
	}
	rt_periodic_timer.expires = now + ip_rt_gc_interval;
	add_timer(&rt_periodic_timer);
}

static void rt_run_flush(unsigned long dummy)
{
	int i;
	struct rtable * rth, * next;

	rt_deadline = 0;

	start_bh_atomic();
	for (i=0; i<RT_HASH_DIVISOR; i++) {
		if ((rth = xchg(&rt_hash_table[i], NULL)) == NULL)
			continue;
		end_bh_atomic();

		for (; rth; rth=next) {
			next = rth->u.rt_next;
			rth->u.rt_next = NULL;
			rt_free(rth);
		}

		start_bh_atomic();
	}
	end_bh_atomic();
}
  
void rt_cache_flush(int delay)
{
	unsigned long now = jiffies;
	int user_mode = !in_interrupt();

	if (delay < 0)
		delay = ip_rt_min_delay;

	start_bh_atomic();

	if (del_timer(&rt_flush_timer) && delay > 0 && rt_deadline) {
		long tmo = (long)(rt_deadline - now);

		/* If flush timer is already running
		   and flush request is not immediate (delay > 0):

		   if deadline is not achieved, prolongate timer to "delay",
		   otherwise fire it at deadline time.
		 */

		if (user_mode && tmo < ip_rt_max_delay-ip_rt_min_delay)
			tmo = 0;
		
		if (delay > tmo)
			delay = tmo;
	}

	if (delay <= 0) {
		end_bh_atomic();
		rt_run_flush(0);
		return;
	}

	if (rt_deadline == 0)
		rt_deadline = now + ip_rt_max_delay;

	rt_flush_timer.expires = now + delay;
	add_timer(&rt_flush_timer);
	end_bh_atomic();
}

/*
   Short description of GC goals.

   We want to build algorithm, which will keep routing cache
   at some equilibrium point, when number of aged off entries
   is kept approximately equal to newly generated ones.

   Current expiration strength is variable "expire".
   We try to adjust it dynamically, so that if networking
   is idle expires is large enough to keep enough of warm entries,
   and when load increases it reduces to limit cache size.
 */

static int rt_garbage_collect(void)
{
	static unsigned expire = RT_GC_TIMEOUT;
	static unsigned long last_gc;
	static int rover;
	static int equilibrium;
	struct rtable *rth, **rthp;
	unsigned long now = jiffies;
	int goal;

	/*
	 * Garbage collection is pretty expensive,
	 * do not make it too frequently.
	 */
	if (now - last_gc < ip_rt_gc_min_interval &&
	    atomic_read(&ipv4_dst_ops.entries) < ip_rt_max_size)
		return 0;

	/* Calculate number of entries, which we want to expire now. */
	goal = atomic_read(&ipv4_dst_ops.entries) - RT_HASH_DIVISOR*ip_rt_gc_elasticity;
	if (goal <= 0) {
		if (equilibrium < ipv4_dst_ops.gc_thresh)
			equilibrium = ipv4_dst_ops.gc_thresh;
		goal = atomic_read(&ipv4_dst_ops.entries) - equilibrium;
		if (goal > 0) {
			equilibrium += min(goal/2, RT_HASH_DIVISOR);
			goal = atomic_read(&ipv4_dst_ops.entries) - equilibrium;
		}
	} else {
		/* We are in dangerous area. Try to reduce cache really
		 * aggressively.
		 */
		goal = max(goal/2, RT_HASH_DIVISOR);
		equilibrium = atomic_read(&ipv4_dst_ops.entries) - goal;
	}

	if (now - last_gc >= ip_rt_gc_min_interval)
		last_gc = now;

	if (goal <= 0) {
		equilibrium += goal;
		goto work_done;
	}

	do {
		int i, k;

		start_bh_atomic();
		for (i=0, k=rover; i<RT_HASH_DIVISOR; i++) {
			unsigned tmo = expire;

			k = (k + 1) & (RT_HASH_DIVISOR-1);
			rthp = &rt_hash_table[k];
			while ((rth = *rthp) != NULL) {
				if (!rt_may_expire(rth, tmo, expire)) {
					tmo >>= 1;
					rthp = &rth->u.rt_next;
					continue;
				}
				*rthp = rth->u.rt_next;
				rth->u.rt_next = NULL;
				rt_free(rth);
				goal--;
			}
			if (goal <= 0)
				break;
		}
		rover = k;
		end_bh_atomic();

		if (goal <= 0)
			goto work_done;

		/* Goal is not achieved. We stop process if:

		   - if expire reduced to zero. Otherwise, expire is halfed.
		   - if table is not full.
		   - if we are called from interrupt.
		   - jiffies check is just fallback/debug loop breaker.
		     We will not spin here for long time in any case.
		 */

		if (expire == 0)
			break;

		expire >>= 1;
#if RT_CACHE_DEBUG >= 2
		printk(KERN_DEBUG "expire>> %u %d %d %d\n", expire, atomic_read(&ipv4_dst_ops.entries), goal, i);
#endif

		if (atomic_read(&ipv4_dst_ops.entries) < ip_rt_max_size)
			return 0;
	} while (!in_interrupt() && jiffies - now < 1);

	if (atomic_read(&ipv4_dst_ops.entries) < ip_rt_max_size)
		return 0;
	if (net_ratelimit())
		printk("dst cache overflow\n");
	return 1;

work_done:
	expire += ip_rt_gc_min_interval;
	if (expire > ip_rt_gc_timeout ||
	    atomic_read(&ipv4_dst_ops.entries) < ipv4_dst_ops.gc_thresh)
		expire = ip_rt_gc_timeout;
#if RT_CACHE_DEBUG >= 2
	printk(KERN_DEBUG "expire++ %u %d %d %d\n", expire, atomic_read(&ipv4_dst_ops.entries), goal, rover);
#endif
	return 0;
}

static int rt_intern_hash(unsigned hash, struct rtable * rt, struct rtable ** rp)
{
	struct rtable	*rth, **rthp;
	unsigned long	now = jiffies;
	int attempts = !in_interrupt();

restart:
	start_bh_atomic();

	rthp = &rt_hash_table[hash];

	while ((rth = *rthp) != NULL) {
		if (memcmp(&rth->key, &rt->key, sizeof(rt->key)) == 0) {
			/* Put it first */
			*rthp = rth->u.rt_next;
			rth->u.rt_next = rt_hash_table[hash];
			rt_hash_table[hash] = rth;

			atomic_inc(&rth->u.dst.refcnt);
			atomic_inc(&rth->u.dst.use);
			rth->u.dst.lastuse = now;
			end_bh_atomic();

			rt_drop(rt);
			*rp = rth;
			return 0;
		}

		rthp = &rth->u.rt_next;
	}

	/* Try to bind route to arp only if it is output
	   route or unicast forwarding path.
	 */
	if (rt->rt_type == RTN_UNICAST || rt->key.iif == 0) {
		if (!arp_bind_neighbour(&rt->u.dst)) {
			end_bh_atomic();

			/* Neighbour tables are full and nothing
			   can be released. Try to shrink route cache,
			   it is most likely it holds some neighbour records.
			 */
			if (attempts-- > 0) {
				int saved_elasticity = ip_rt_gc_elasticity;
				int saved_int = ip_rt_gc_min_interval;
				ip_rt_gc_elasticity = 1;
				ip_rt_gc_min_interval = 0;
				rt_garbage_collect();
				ip_rt_gc_min_interval = saved_int;
				ip_rt_gc_elasticity = saved_elasticity;
				goto restart;
			}

			rt_drop(rt);
			if (net_ratelimit())
				printk("neighbour table overflow\n");
			return -ENOBUFS;
		}
	}

	rt->u.rt_next = rt_hash_table[hash];
#if RT_CACHE_DEBUG >= 2
	if (rt->u.rt_next) {
		struct rtable * trt;
		printk("rt_cache @%02x: %08x", hash, rt->rt_dst);
		for (trt=rt->u.rt_next; trt; trt=trt->u.rt_next)
			printk(" . %08x", trt->rt_dst);
		printk("\n");
	}
#endif
	rt_hash_table[hash] = rt;
	end_bh_atomic();
	*rp = rt;
	return 0;
}

static void rt_del(unsigned hash, struct rtable *rt)
{
	struct rtable **rthp;

	start_bh_atomic();
	ip_rt_put(rt);
	for (rthp = &rt_hash_table[hash]; *rthp; rthp = &(*rthp)->u.rt_next) {
		if (*rthp == rt) {
			*rthp = rt->u.rt_next;
			rt_free(rt);
			break;
		}
	}
	end_bh_atomic();
}

void ip_rt_redirect(u32 old_gw, u32 daddr, u32 new_gw,
		    u32 saddr, u8 tos, struct device *dev)
{
	int i, k;
	struct in_device *in_dev = dev->ip_ptr;
	struct rtable *rth, **rthp;
	u32  skeys[2] = { saddr, 0 };
	int  ikeys[2] = { dev->ifindex, 0 };

	tos &= IPTOS_TOS_MASK;

	if (!in_dev)
		return;

	if (new_gw == old_gw || !IN_DEV_RX_REDIRECTS(in_dev)
	    || MULTICAST(new_gw) || BADCLASS(new_gw) || ZERONET(new_gw))
		goto reject_redirect;

	if (!IN_DEV_SHARED_MEDIA(in_dev)) {
		if (!inet_addr_onlink(in_dev, new_gw, old_gw))
			goto reject_redirect;
		if (IN_DEV_SEC_REDIRECTS(in_dev) && ip_fib_check_default(new_gw, dev))
			goto reject_redirect;
	} else {
		if (inet_addr_type(new_gw) != RTN_UNICAST)
			goto reject_redirect;
	}

	for (i=0; i<2; i++) {
		for (k=0; k<2; k++) {
			unsigned hash = rt_hash_code(daddr, skeys[i]^(ikeys[k]<<5), tos);

			rthp=&rt_hash_table[hash];

			while ( (rth = *rthp) != NULL) {
				struct rtable *rt;

				if (rth->key.dst != daddr ||
				    rth->key.src != skeys[i] ||
				    rth->key.tos != tos ||
				    rth->key.oif != ikeys[k] ||
				    rth->key.iif != 0) {
					rthp = &rth->u.rt_next;
					continue;
				}

				if (rth->rt_dst != daddr ||
				    rth->rt_src != saddr ||
				    rth->u.dst.error ||
				    rth->rt_gateway != old_gw ||
				    rth->u.dst.dev != dev)
					break;

				dst_clone(&rth->u.dst);

				rt = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
				if (rt == NULL) {
					ip_rt_put(rth);
					return;
				}

				/*
				 * Copy all the information.
				 */
				*rt = *rth;
				atomic_set(&rt->u.dst.refcnt, 1);
				atomic_set(&rt->u.dst.use, 1);
				rt->u.dst.lastuse = jiffies;
				rt->u.dst.neighbour = NULL;
				rt->u.dst.hh = NULL;
				rt->u.dst.obsolete = 0;

				rt->rt_flags |= RTCF_REDIRECTED;

				/* Gateway is different ... */
				rt->rt_gateway = new_gw;

				/* Redirect received -> path was valid */
				dst_confirm(&rth->u.dst);

				if (!arp_bind_neighbour(&rt->u.dst) ||
				    !(rt->u.dst.neighbour->nud_state&NUD_VALID)) {
					if (rt->u.dst.neighbour)
						neigh_event_send(rt->u.dst.neighbour, NULL);
					ip_rt_put(rth);
					rt_drop(rt);
					break;
				}

				rt_del(hash, rth);

				if (!rt_intern_hash(hash, rt, &rt))
					ip_rt_put(rt);
				break;
			}
		}
	}
	return;

reject_redirect:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev) && net_ratelimit())
		printk(KERN_INFO "Redirect from %X/%s to %X ignored."
		       "Path = %X -> %X, tos %02x\n",
		       ntohl(old_gw), dev->name, ntohl(new_gw),
		       ntohl(saddr), ntohl(daddr), tos);
#else
	; /* Do nothing.  */
#endif
}

static struct dst_entry *ipv4_negative_advice(struct dst_entry *dst)
{
	struct rtable *rt = (struct rtable*)dst;

	if (rt != NULL) {
		if (dst->obsolete) {
			ip_rt_put(rt);
			return NULL;
		}
		if ((rt->rt_flags&RTCF_REDIRECTED) || rt->u.dst.expires) {
			unsigned hash = rt_hash_code(rt->key.dst, rt->key.src^(rt->key.oif<<5), rt->key.tos);
#if RT_CACHE_DEBUG >= 1
			printk(KERN_DEBUG "ip_rt_advice: redirect to %d.%d.%d.%d/%02x dropped\n", NIPQUAD(rt->rt_dst), rt->key.tos);
#endif
			rt_del(hash, rt);
			return NULL;
		}
	}
	return dst;
}

/*
 * Algorithm:
 *	1. The first ip_rt_redirect_number redirects are sent
 *	   with exponential backoff, then we stop sending them at all,
 *	   assuming that the host ignores our redirects.
 *	2. If we did not see packets requiring redirects
 *	   during ip_rt_redirect_silence, we assume that the host
 *	   forgot redirected route and start to send redirects again.
 *
 * This algorithm is much cheaper and more intelligent than dumb load limiting
 * in icmp.c.
 *
 * NOTE. Do not forget to inhibit load limiting for redirects (redundant)
 * and "frag. need" (breaks PMTU discovery) in icmp.c.
 */

void ip_rt_send_redirect(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct in_device *in_dev = (struct in_device*)rt->u.dst.dev->ip_ptr;

	if (!in_dev || !IN_DEV_TX_REDIRECTS(in_dev))
		return;

	/* No redirected packets during ip_rt_redirect_silence;
	 * reset the algorithm.
	 */
	if (jiffies - rt->u.dst.rate_last > ip_rt_redirect_silence)
		rt->u.dst.rate_tokens = 0;

	/* Too many ignored redirects; do not send anything
	 * set u.dst.rate_last to the last seen redirected packet.
	 */
	if (rt->u.dst.rate_tokens >= ip_rt_redirect_number) {
		rt->u.dst.rate_last = jiffies;
		return;
	}

	/* Check for load limit; set rate_last to the latest sent
	 * redirect.
	 */
	if (jiffies - rt->u.dst.rate_last > (ip_rt_redirect_load<<rt->u.dst.rate_tokens)) {
		icmp_send(skb, ICMP_REDIRECT, ICMP_REDIR_HOST, rt->rt_gateway);
		rt->u.dst.rate_last = jiffies;
		++rt->u.dst.rate_tokens;
#ifdef CONFIG_IP_ROUTE_VERBOSE
		if (IN_DEV_LOG_MARTIANS(in_dev) &&
		    rt->u.dst.rate_tokens == ip_rt_redirect_number && net_ratelimit())
			printk(KERN_WARNING "host %08x/if%d ignores redirects for %08x to %08x.\n",
			       rt->rt_src, rt->rt_iif, rt->rt_dst, rt->rt_gateway);
#endif
	}
}

static int ip_error(struct sk_buff *skb)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	unsigned long now;
	int code;

	switch (rt->u.dst.error) {
	case EINVAL:
	default:
		kfree_skb(skb);
		return 0;
	case EHOSTUNREACH:
		code = ICMP_HOST_UNREACH;
		break;
	case ENETUNREACH:
		code = ICMP_NET_UNREACH;
		break;
	case EACCES:
		code = ICMP_PKT_FILTERED;
		break;
	}

	now = jiffies;
	if ((rt->u.dst.rate_tokens += (now - rt->u.dst.rate_last)) > ip_rt_error_burst)
		rt->u.dst.rate_tokens = ip_rt_error_burst;
	rt->u.dst.rate_last = now;
	if (rt->u.dst.rate_tokens >= ip_rt_error_cost) {
		rt->u.dst.rate_tokens -= ip_rt_error_cost;
		icmp_send(skb, ICMP_DEST_UNREACH, code, 0);
	}

	kfree_skb(skb);
	return 0;
} 

/*
 *	The last two values are not from the RFC but
 *	are needed for AMPRnet AX.25 paths.
 */

static unsigned short mtu_plateau[] =
{32000, 17914, 8166, 4352, 2002, 1492, 576, 296, 216, 128 };

static __inline__ unsigned short guess_mtu(unsigned short old_mtu)
{
	int i;
	
	for (i = 0; i < sizeof(mtu_plateau)/sizeof(mtu_plateau[0]); i++)
		if (old_mtu > mtu_plateau[i])
			return mtu_plateau[i];
	return 68;
}

unsigned short ip_rt_frag_needed(struct iphdr *iph, unsigned short new_mtu)
{
	int i;
	unsigned short old_mtu = ntohs(iph->tot_len);
	struct rtable *rth;
	u32  skeys[2] = { iph->saddr, 0, };
	u32  daddr = iph->daddr;
	u8   tos = iph->tos & IPTOS_TOS_MASK;
	unsigned short est_mtu = 0;

	if (ipv4_config.no_pmtu_disc)
		return 0;

	for (i=0; i<2; i++) {
		unsigned hash = rt_hash_code(daddr, skeys[i], tos);

		for (rth = rt_hash_table[hash]; rth; rth = rth->u.rt_next) {
			if (rth->key.dst == daddr &&
			    rth->key.src == skeys[i] &&
			    rth->rt_dst == daddr &&
			    rth->rt_src == iph->saddr &&
			    rth->key.tos == tos &&
			    rth->key.iif == 0 &&
			    !(rth->u.dst.mxlock&(1<<RTAX_MTU))) {
				unsigned short mtu = new_mtu;

				if (new_mtu < 68 || new_mtu >= old_mtu) {

					/* BSD 4.2 compatibility hack :-( */
					if (mtu == 0 && old_mtu >= rth->u.dst.pmtu &&
					    old_mtu >= 68 + (iph->ihl<<2))
						old_mtu -= iph->ihl<<2;

					mtu = guess_mtu(old_mtu);
				}
				if (mtu <= rth->u.dst.pmtu) {
					if (mtu < rth->u.dst.pmtu) { 
						dst_confirm(&rth->u.dst);
						rth->u.dst.pmtu = mtu;
						dst_set_expires(&rth->u.dst, ip_rt_mtu_expires);
					}
					est_mtu = mtu;
				}
			}
		}
	}
	return est_mtu ? : new_mtu;
}

void ip_rt_update_pmtu(struct dst_entry *dst, unsigned mtu)
{
	if (dst->pmtu > mtu && mtu >= 68 &&
	    !(dst->mxlock&(1<<RTAX_MTU))) {
		dst->pmtu = mtu;
		dst_set_expires(dst, ip_rt_mtu_expires);
	}
}

static struct dst_entry * ipv4_dst_check(struct dst_entry * dst, u32 cookie)
{
	dst_release(dst);
	return NULL;
}

static struct dst_entry * ipv4_dst_reroute(struct dst_entry * dst,
					   struct sk_buff *skb)
{
	return NULL;
}

static void ipv4_link_failure(struct sk_buff *skb)
{
	struct rtable *rt;

	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);

	rt = (struct rtable *) skb->dst;
	if (rt)
		dst_set_expires(&rt->u.dst, 0);
}

static int ip_rt_bug(struct sk_buff *skb)
{
	printk(KERN_DEBUG "ip_rt_bug: %08x -> %08x, %s\n", skb->nh.iph->saddr,
	       skb->nh.iph->daddr, skb->dev ? skb->dev->name : "?");
	kfree_skb(skb);
	return 0;
}

/*
   We do not cache source address of outgoing interface,
   because it is used only by IP RR, TS and SRR options,
   so that it out of fast path.

   BTW remember: "addr" is allowed to be not aligned
   in IP options!
 */

void ip_rt_get_source(u8 *addr, struct rtable *rt)
{
	u32 src;
	struct fib_result res;

	if (rt->key.iif == 0)
		src = rt->rt_src;
	else if (fib_lookup(&rt->key, &res) == 0 && res.type != RTN_NAT)
		src = FIB_RES_PREFSRC(res);
	else
		src = inet_select_addr(rt->u.dst.dev, rt->rt_gateway, RT_SCOPE_UNIVERSE);
	memcpy(addr, &src, 4);
}

#ifdef CONFIG_NET_CLS_ROUTE
static void set_class_tag(struct rtable *rt, u32 tag)
{
	if (!(rt->u.dst.tclassid&0xFFFF))
		rt->u.dst.tclassid |= tag&0xFFFF;
	if (!(rt->u.dst.tclassid&0xFFFF0000))
		rt->u.dst.tclassid |= tag&0xFFFF0000;
}
#endif

static void rt_set_nexthop(struct rtable *rt, struct fib_result *res, u32 itag)
{
	struct fib_info *fi = res->fi;

	if (fi) {
		if (FIB_RES_GW(*res) && FIB_RES_NH(*res).nh_scope == RT_SCOPE_LINK)
			rt->rt_gateway = FIB_RES_GW(*res);
		rt->u.dst.mxlock = fi->fib_metrics[RTAX_LOCK-1];
		rt->u.dst.pmtu = fi->fib_mtu;
		if (fi->fib_mtu == 0) {
			rt->u.dst.pmtu = rt->u.dst.dev->mtu;
			if (rt->u.dst.pmtu > IP_MAX_MTU)
				rt->u.dst.pmtu = IP_MAX_MTU;
			if (rt->u.dst.pmtu < 68)
				rt->u.dst.pmtu = 68;
			if (rt->u.dst.mxlock&(1<<RTAX_MTU) &&
			    rt->rt_gateway != rt->rt_dst &&
			    rt->u.dst.pmtu > 576)
				rt->u.dst.pmtu = 576;
		}
		rt->u.dst.window= fi->fib_window ? : 0;
		rt->u.dst.rtt	= fi->fib_rtt ? : TCP_TIMEOUT_INIT;
#ifdef CONFIG_NET_CLS_ROUTE
		rt->u.dst.tclassid = FIB_RES_NH(*res).nh_tclassid;
#endif
	} else {
		rt->u.dst.pmtu	= rt->u.dst.dev->mtu;
		if (rt->u.dst.pmtu > IP_MAX_MTU)
			rt->u.dst.pmtu = IP_MAX_MTU;
		if (rt->u.dst.pmtu < 68)
			rt->u.dst.pmtu = 68;
		rt->u.dst.window= 0;
		rt->u.dst.rtt	= TCP_TIMEOUT_INIT;
	}
#ifdef CONFIG_NET_CLS_ROUTE
#ifdef CONFIG_IP_MULTIPLE_TABLES
	set_class_tag(rt, fib_rules_tclass(res));
#endif
	set_class_tag(rt, itag);
#endif
        rt->rt_type = res->type;
}

static int
ip_route_input_mc(struct sk_buff *skb, u32 daddr, u32 saddr,
		  u8 tos, struct device *dev, int our)
{
	unsigned hash;
	struct rtable *rth;
	u32 spec_dst;
	struct in_device *in_dev = dev->ip_ptr;
	u32 itag = 0;

	/* Primary sanity checks. */

	if (MULTICAST(saddr) || BADCLASS(saddr) || LOOPBACK(saddr) ||
	    in_dev == NULL || skb->protocol != __constant_htons(ETH_P_IP))
		return -EINVAL;

	if (ZERONET(saddr)) {
		if (!LOCAL_MCAST(daddr))
			return -EINVAL;
		spec_dst = inet_select_addr(dev, 0, RT_SCOPE_LINK);
	} else if (fib_validate_source(saddr, 0, tos, 0, dev, &spec_dst, &itag) < 0)
		return -EINVAL;

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.output= ip_rt_bug;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	rth->key.fwmark	= skb->fwmark;
#endif
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= daddr;
	rth->rt_src_map	= saddr;
#endif
#ifdef CONFIG_NET_CLS_ROUTE
	rth->u.dst.tclassid = itag;
#endif
	rth->rt_iif	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= &loopback_dev;
	rth->key.oif	= 0;
	rth->rt_gateway	= daddr;
	rth->rt_spec_dst= spec_dst;
	rth->rt_type	= RTN_MULTICAST;
	rth->rt_flags	= RTCF_MULTICAST;
	if (our) {
		rth->u.dst.input= ip_local_deliver;
		rth->rt_flags |= RTCF_LOCAL;
	}

#ifdef CONFIG_IP_MROUTE
	if (!LOCAL_MCAST(daddr) && IN_DEV_MFORWARD(in_dev))
		rth->u.dst.input = ip_mr_input;
#endif

	hash = rt_hash_code(daddr, saddr^(dev->ifindex<<5), tos);
	return rt_intern_hash(hash, rth, (struct rtable**)&skb->dst);
}

/*
 *	NOTE. We drop all the packets that has local source
 *	addresses, because every properly looped back packet
 *	must have correct destination already attached by output routine.
 *
 *	Such approach solves two big problems:
 *	1. Not simplex devices are handled properly.
 *	2. IP spoofing attempts are filtered with 100% of guarantee.
 */

int ip_route_input_slow(struct sk_buff *skb, u32 daddr, u32 saddr,
			u8 tos, struct device *dev)
{
	struct rt_key	key;
	struct fib_result res;
	struct in_device *in_dev = dev->ip_ptr;
	struct in_device *out_dev;
	unsigned	flags = 0;
	u32		itag = 0;
	struct rtable * rth;
	unsigned	hash;
	u32		spec_dst;
	int		err = -EINVAL;

	/*
	 *	IP on this device is disabled.
	 */

	if (!in_dev)
		return -EINVAL;

	key.dst = daddr;
	key.src = saddr;
	key.tos = tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	key.fwmark = skb->fwmark;
#endif
	key.iif = dev->ifindex;
	key.oif = 0;
	key.scope = RT_SCOPE_UNIVERSE;

	hash = rt_hash_code(daddr, saddr^(key.iif<<5), tos);

	/* Check for the most weird martians, which can be not detected
	   by fib_lookup.
	 */

	if (MULTICAST(saddr) || BADCLASS(saddr) || LOOPBACK(saddr))
		goto martian_source;

	if (daddr == 0xFFFFFFFF || (saddr == 0 && daddr == 0))
		goto brd_input;

	/* Accept zero addresses only to limited broadcast;
	 * I even do not know to fix it or not. Waiting for complains :-)
	 */
	if (ZERONET(saddr))
		goto martian_source;

	if (BADCLASS(daddr) || ZERONET(daddr) || LOOPBACK(daddr))
		goto martian_destination;

	/*
	 *	Now we are ready to route packet.
	 */
	if ((err = fib_lookup(&key, &res))) {
		if (!IN_DEV_FORWARD(in_dev))
			return -EINVAL;
		goto no_route;
	}

#ifdef CONFIG_IP_ROUTE_NAT
	/* Policy is applied before mapping destination,
	   but rerouting after map should be made with old source.
	 */

	if (1) {
		u32 src_map = saddr;
		if (res.r)
			src_map = fib_rules_policy(saddr, &res, &flags);

		if (res.type == RTN_NAT) {
			key.dst = fib_rules_map_destination(daddr, &res);
			if (fib_lookup(&key, &res) || res.type != RTN_UNICAST)
				return -EINVAL;
			flags |= RTCF_DNAT;
		}
		key.src = src_map;
	}
#endif

	if (res.type == RTN_BROADCAST)
		goto brd_input;

	if (res.type == RTN_LOCAL) {
		int result;
		result = fib_validate_source(saddr, daddr, tos, loopback_dev.ifindex,
					     dev, &spec_dst, &itag);
		if (result < 0)
			goto martian_source;
		if (result)
			flags |= RTCF_DIRECTSRC;
		spec_dst = daddr;
		goto local_input;
	}

	if (!IN_DEV_FORWARD(in_dev))
		return -EINVAL;
	if (res.type != RTN_UNICAST)
		goto martian_destination;

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res.fi->fib_nhs > 1 && key.oif == 0)
		fib_select_multipath(&key, &res);
#endif
	out_dev = FIB_RES_DEV(res)->ip_ptr;
	if (out_dev == NULL) {
		if (net_ratelimit())
			printk(KERN_CRIT "Bug in ip_route_input_slow(). Please, report\n");
		return -EINVAL;
	}

	err = fib_validate_source(saddr, daddr, tos, FIB_RES_OIF(res), dev, &spec_dst, &itag);
	if (err < 0)
		goto martian_source;

	if (err)
		flags |= RTCF_DIRECTSRC;

	if (out_dev == in_dev && err && !(flags&(RTCF_NAT|RTCF_MASQ)) &&
	    (IN_DEV_SHARED_MEDIA(out_dev)
	     || inet_addr_onlink(out_dev, saddr, FIB_RES_GW(res))))
		flags |= RTCF_DOREDIRECT;

	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		/* Not IP (i.e. ARP). Do not create route, if it is
		 * invalid for proxy arp. DNAT routes are always valid.
		 */
		if (out_dev == in_dev && !(flags&RTCF_DNAT))
			return -EINVAL;
	}

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	rth->key.fwmark	= skb->fwmark;
#endif
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
	rth->rt_gateway	= daddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_src_map	= key.src;
	rth->rt_dst_map	= key.dst;
	if (flags&RTCF_DNAT)
		rth->rt_gateway	= key.dst;
#endif
	rth->rt_iif 	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= out_dev->dev;
	rth->key.oif 	= 0;
	rth->rt_spec_dst= spec_dst;

	rth->u.dst.input = ip_forward;
	rth->u.dst.output = ip_output;

	rt_set_nexthop(rth, &res, itag);

	rth->rt_flags = flags;

#ifdef CONFIG_NET_FASTROUTE
	if (netdev_fastroute && !(flags&(RTCF_NAT|RTCF_MASQ|RTCF_DOREDIRECT))) {
		struct device *odev = rth->u.dst.dev;
		if (odev != dev &&
		    dev->accept_fastpath &&
		    odev->mtu >= dev->mtu &&
		    dev->accept_fastpath(dev, &rth->u.dst) == 0)
			rth->rt_flags |= RTCF_FAST;
	}
#endif

	return rt_intern_hash(hash, rth, (struct rtable**)&skb->dst);

brd_input:
	if (skb->protocol != __constant_htons(ETH_P_IP))
		return -EINVAL;

	if (ZERONET(saddr)) {
		spec_dst = inet_select_addr(dev, 0, RT_SCOPE_LINK);
	} else {
		err = fib_validate_source(saddr, 0, tos, 0, dev, &spec_dst, &itag);
		if (err < 0)
			goto martian_source;
		if (err)
			flags |= RTCF_DIRECTSRC;
	}
	flags |= RTCF_BROADCAST;
	res.type = RTN_BROADCAST;

local_input:
	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	rth->u.dst.output= ip_rt_bug;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->rt_dst	= daddr;
	rth->key.tos	= tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	rth->key.fwmark	= skb->fwmark;
#endif
	rth->key.src	= saddr;
	rth->rt_src	= saddr;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= key.dst;
	rth->rt_src_map	= key.src;
#endif
#ifdef CONFIG_NET_CLS_ROUTE
	rth->u.dst.tclassid = itag;
#endif
	rth->rt_iif	=
	rth->key.iif	= dev->ifindex;
	rth->u.dst.dev	= &loopback_dev;
	rth->key.oif 	= 0;
	rth->rt_gateway	= daddr;
	rth->rt_spec_dst= spec_dst;
	rth->u.dst.input= ip_local_deliver;
	rth->rt_flags 	= flags|RTCF_LOCAL;
	if (res.type == RTN_UNREACHABLE) {
		rth->u.dst.input= ip_error;
		rth->u.dst.error= -err;
		rth->rt_flags 	&= ~RTCF_LOCAL;
	}
	rth->rt_type	= res.type;
	return rt_intern_hash(hash, rth, (struct rtable**)&skb->dst);

no_route:
	spec_dst = inet_select_addr(dev, 0, RT_SCOPE_UNIVERSE);
	res.type = RTN_UNREACHABLE;
	goto local_input;

	/*
	 *	Do not cache martian addresses: they should be logged (RFC1812)
	 */
martian_destination:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev) && net_ratelimit())
		printk(KERN_WARNING "martian destination %08x from %08x, dev %s\n", daddr, saddr, dev->name);
#endif
	return -EINVAL;

martian_source:
#ifdef CONFIG_IP_ROUTE_VERBOSE
	if (IN_DEV_LOG_MARTIANS(in_dev) && net_ratelimit()) {
		/*
		 *	RFC1812 recommenadtion, if source is martian,
		 *	the only hint is MAC header.
		 */
		printk(KERN_WARNING "martian source %08x for %08x, dev %s\n", saddr, daddr, dev->name);
		if (dev->hard_header_len) {
			int i;
			unsigned char *p = skb->mac.raw;
			printk(KERN_WARNING "ll header:");
			for (i=0; i<dev->hard_header_len; i++, p++)
				printk(" %02x", *p);
			printk("\n");
		}
	}
#endif
	return -EINVAL;
}

int ip_route_input(struct sk_buff *skb, u32 daddr, u32 saddr,
		   u8 tos, struct device *dev)
{
	struct rtable * rth;
	unsigned	hash;
	int iif = dev->ifindex;

	tos &= IPTOS_TOS_MASK;
	hash = rt_hash_code(daddr, saddr^(iif<<5), tos);

	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.iif == iif &&
		    rth->key.oif == 0 &&
#ifdef CONFIG_IP_ROUTE_FWMARK
		    rth->key.fwmark == skb->fwmark &&
#endif
		    rth->key.tos == tos) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			skb->dst = (struct dst_entry*)rth;
			return 0;
		}
	}

	/* Multicast recognition logic is moved from route cache to here.
	   The problem was that too many Ethernet cards have broken/missing
	   hardware multicast filters :-( As result the host on multicasting
	   network acquires a lot of useless route cache entries, sort of
	   SDR messages from all the world. Now we try to get rid of them.
	   Really, provided software IP multicast filter is organized
	   reasonably (at least, hashed), it does not result in a slowdown
	   comparing with route cache reject entries.
	   Note, that multicast routers are not affected, because
	   route cache entry is created eventually.
	 */
	if (MULTICAST(daddr)) {
		int our = ip_check_mc(dev, daddr);
		if (!our
#ifdef CONFIG_IP_MROUTE
		    && (LOCAL_MCAST(daddr) || !dev->ip_ptr ||
			!IN_DEV_MFORWARD((struct in_device*)dev->ip_ptr))
#endif
		    ) return -EINVAL;
		return ip_route_input_mc(skb, daddr, saddr, tos, dev, our);
	}
	return ip_route_input_slow(skb, daddr, saddr, tos, dev);
}

/*
 * Major route resolver routine.
 */

int ip_route_output_slow(struct rtable **rp, u32 daddr, u32 saddr, u32 tos, int oif)
{
	struct rt_key key;
	struct fib_result res;
	unsigned flags = 0;
	struct rtable *rth;
	struct device *dev_out = NULL;
	unsigned hash;
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	u32 nochecksrc = (tos & RTO_TPROXY);
#endif

	tos &= IPTOS_TOS_MASK|RTO_ONLINK;
	key.dst = daddr;
	key.src = saddr;
	key.tos = tos&IPTOS_TOS_MASK;
	key.iif = loopback_dev.ifindex;
	key.oif = oif;
	key.scope = (tos&RTO_ONLINK) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
	res.fi = NULL;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	res.r = NULL;
#endif

	if (saddr) {
		if (MULTICAST(saddr) || BADCLASS(saddr) || ZERONET(saddr))
			return -EINVAL;

		/* It is equivalent to inet_addr_type(saddr) == RTN_LOCAL */
		dev_out = ip_dev_find(saddr);
#ifdef CONFIG_IP_TRANSPARENT_PROXY
		/* If address is not local, test for transparent proxy flag;
		   if address is local --- clear the flag.
		 */
		if (dev_out == NULL) {
			if (nochecksrc == 0 || inet_addr_type(saddr) != RTN_UNICAST)
				return -EINVAL;
			flags |= RTCF_TPROXY;
		}
#else
		if (dev_out == NULL)
			return -EINVAL;
#endif

		/* I removed check for oif == dev_out->oif here.
		   It was wrong by three reasons:
		   1. ip_dev_find(saddr) can return wrong iface, if saddr is
		      assigned to multiple interfaces.
		   2. Moreover, we are allowed to send packets with saddr
		      of another iface. --ANK
		 */

		if (oif == 0 &&
#ifdef CONFIG_IP_TRANSPARENT_PROXY
			dev_out &&
#endif
			(MULTICAST(daddr) || daddr == 0xFFFFFFFF)) {
			/* Special hack: user can direct multicasts
			   and limited broadcast via necessary interface
			   without fiddling with IP_MULTICAST_IF or IP_PKTINFO.
			   This hack is not just for fun, it allows
			   vic,vat and friends to work.
			   They bind socket to loopback, set ttl to zero
			   and expect that it will work.
			   From the viewpoint of routing cache they are broken,
			   because we are not allowed to build multicast path
			   with loopback source addr (look, routing cache
			   cannot know, that ttl is zero, so that packet
			   will not leave this host and route is valid).
			   Luckily, this hack is good workaround.
			 */

			key.oif = dev_out->ifindex;
			goto make_route;
		}
		dev_out = NULL;
	}
	if (oif) {
		dev_out = dev_get_by_index(oif);
		if (dev_out == NULL)
			return -ENODEV;
		if (dev_out->ip_ptr == NULL)
			return -ENODEV;	/* Wrong error code */

		if (LOCAL_MCAST(daddr) || daddr == 0xFFFFFFFF) {
			if (!key.src)
				key.src = inet_select_addr(dev_out, 0, RT_SCOPE_LINK);
			goto make_route;
		}
		if (!key.src) {
			if (MULTICAST(daddr))
				key.src = inet_select_addr(dev_out, 0, key.scope);
			else if (!daddr)
				key.src = inet_select_addr(dev_out, 0, RT_SCOPE_HOST);
		}
	}

	if (!key.dst) {
		key.dst = key.src;
		if (!key.dst)
			key.dst = key.src = htonl(INADDR_LOOPBACK);
		dev_out = &loopback_dev;
		key.oif = loopback_dev.ifindex;
		res.type = RTN_LOCAL;
		flags |= RTCF_LOCAL;
		goto make_route;
	}

	if (fib_lookup(&key, &res)) {
		res.fi = NULL;
		if (oif) {
			/* Apparently, routing tables are wrong. Assume,
			   that the destination is on link.

			   WHY? DW.
			   Because we are allowed to send to iface
			   even if it has NO routes and NO assigned
			   addresses. When oif is specified, routing
			   tables are looked up with only one purpose:
			   to catch if destination is gatewayed, rather than
			   direct. Moreover, if MSG_DONTROUTE is set,
			   we send packet, ignoring both routing tables
			   and ifaddr state. --ANK


			   We could make it even if oif is unknown,
			   likely IPv6, but we do not.
			 */

			if (key.src == 0)
				key.src = inet_select_addr(dev_out, 0, RT_SCOPE_LINK);
			res.type = RTN_UNICAST;
			goto make_route;
		}
		return -ENETUNREACH;
	}

	if (res.type == RTN_NAT)
		return -EINVAL;

	if (res.type == RTN_LOCAL) {
		if (!key.src)
			key.src = key.dst;
		dev_out = &loopback_dev;
		key.oif = dev_out->ifindex;
		res.fi = NULL;
		flags |= RTCF_LOCAL;
		goto make_route;
	}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (res.fi->fib_nhs > 1 && key.oif == 0)
		fib_select_multipath(&key, &res);
	else
#endif
	if (res.prefixlen==0 && res.type == RTN_UNICAST && key.oif == 0)
		fib_select_default(&key, &res);

	if (!key.src)
		key.src = FIB_RES_PREFSRC(res);

	dev_out = FIB_RES_DEV(res);
	key.oif = dev_out->ifindex;

make_route:
	if (LOOPBACK(key.src) && !(dev_out->flags&IFF_LOOPBACK))
		return -EINVAL;

	if (key.dst == 0xFFFFFFFF)
		res.type = RTN_BROADCAST;
	else if (MULTICAST(key.dst))
		res.type = RTN_MULTICAST;
	else if (BADCLASS(key.dst) || ZERONET(key.dst))
		return -EINVAL;

	if (dev_out->flags&IFF_LOOPBACK)
		flags |= RTCF_LOCAL;

	if (res.type == RTN_BROADCAST) {
		flags |= RTCF_BROADCAST|RTCF_LOCAL;
		res.fi = NULL;
	} else if (res.type == RTN_MULTICAST) {
		flags |= RTCF_MULTICAST|RTCF_LOCAL;
		if (!ip_check_mc(dev_out, daddr))
			flags &= ~RTCF_LOCAL;
		/* If multicast route do not exist use
		   default one, but do not gateway in this case.
		   Yes, it is hack.
		 */
		if (res.fi && res.prefixlen < 4)
			res.fi = NULL;
	}

	rth = dst_alloc(sizeof(struct rtable), &ipv4_dst_ops);
	if (!rth)
		return -ENOBUFS;

	atomic_set(&rth->u.dst.use, 1);
	rth->key.dst	= daddr;
	rth->key.tos	= tos;
	rth->key.src	= saddr;
	rth->key.iif	= 0;
	rth->key.oif	= oif;
	rth->rt_dst	= key.dst;
	rth->rt_src	= key.src;
#ifdef CONFIG_IP_ROUTE_NAT
	rth->rt_dst_map	= key.dst;
	rth->rt_src_map	= key.src;
#endif
	rth->rt_iif	= oif ? : dev_out->ifindex;
	rth->u.dst.dev	= dev_out;
	rth->rt_gateway = key.dst;
	rth->rt_spec_dst= key.src;

	rth->u.dst.output=ip_output;

	if (flags&RTCF_LOCAL) {
		rth->u.dst.input = ip_local_deliver;
		rth->rt_spec_dst = key.dst;
	}
	if (flags&(RTCF_BROADCAST|RTCF_MULTICAST)) {
		rth->rt_spec_dst = key.src;
		if (flags&RTCF_LOCAL && !(dev_out->flags&IFF_LOOPBACK))
			rth->u.dst.output = ip_mc_output;
#ifdef CONFIG_IP_MROUTE
		if (res.type == RTN_MULTICAST && dev_out->ip_ptr) {
			struct in_device *in_dev = dev_out->ip_ptr;
			if (IN_DEV_MFORWARD(in_dev) && !LOCAL_MCAST(daddr)) {
				rth->u.dst.input = ip_mr_input;
				rth->u.dst.output = ip_mc_output;
			}
		}
#endif
	}

	rt_set_nexthop(rth, &res, 0);

	rth->rt_flags = flags;

	hash = rt_hash_code(daddr, saddr^(oif<<5), tos);
	return rt_intern_hash(hash, rth, rp);
}

int ip_route_output(struct rtable **rp, u32 daddr, u32 saddr, u32 tos, int oif)
{
	unsigned hash;
	struct rtable *rth;

	hash = rt_hash_code(daddr, saddr^(oif<<5), tos);

	start_bh_atomic();
	for (rth=rt_hash_table[hash]; rth; rth=rth->u.rt_next) {
		if (rth->key.dst == daddr &&
		    rth->key.src == saddr &&
		    rth->key.iif == 0 &&
		    rth->key.oif == oif &&
#ifndef CONFIG_IP_TRANSPARENT_PROXY
		    rth->key.tos == tos
#else
		    !((rth->key.tos^tos)&(IPTOS_TOS_MASK|RTO_ONLINK)) &&
		    ((tos&RTO_TPROXY) || !(rth->rt_flags&RTCF_TPROXY))
#endif
		) {
			rth->u.dst.lastuse = jiffies;
			atomic_inc(&rth->u.dst.use);
			atomic_inc(&rth->u.dst.refcnt);
			end_bh_atomic();
			*rp = rth;
			return 0;
		}
	}
	end_bh_atomic();

	return ip_route_output_slow(rp, daddr, saddr, tos, oif);
}

#ifdef CONFIG_RTNETLINK

static int rt_fill_info(struct sk_buff *skb, u32 pid, u32 seq, int event, int nowait)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct rtmsg *r;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;
	struct rta_cacheinfo ci;
#ifdef CONFIG_IP_MROUTE
	struct rtattr *eptr;
#endif
	struct rtattr *mx;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*r));
	r = NLMSG_DATA(nlh);
	nlh->nlmsg_flags = (nowait && pid) ? NLM_F_MULTI : 0;
	r->rtm_family = AF_INET;
	r->rtm_dst_len = 32;
	r->rtm_src_len = 0;
	r->rtm_tos = rt->key.tos;
	r->rtm_table = RT_TABLE_MAIN;
	r->rtm_type = rt->rt_type;
	r->rtm_scope = RT_SCOPE_UNIVERSE;
	r->rtm_protocol = RTPROT_UNSPEC;
	r->rtm_flags = (rt->rt_flags&~0xFFFF) | RTM_F_CLONED;
	if (rt->rt_flags & RTCF_NOTIFY)
		r->rtm_flags |= RTM_F_NOTIFY;
	RTA_PUT(skb, RTA_DST, 4, &rt->rt_dst);
	if (rt->key.src) {
		r->rtm_src_len = 32;
		RTA_PUT(skb, RTA_SRC, 4, &rt->key.src);
	}
	if (rt->u.dst.dev)
		RTA_PUT(skb, RTA_OIF, sizeof(int), &rt->u.dst.dev->ifindex);
#ifdef CONFIG_NET_CLS_ROUTE
	if (rt->u.dst.tclassid)
		RTA_PUT(skb, RTA_FLOW, 4, &rt->u.dst.tclassid);
#endif
	if (rt->key.iif)
		RTA_PUT(skb, RTA_PREFSRC, 4, &rt->rt_spec_dst);
	else if (rt->rt_src != rt->key.src)
		RTA_PUT(skb, RTA_PREFSRC, 4, &rt->rt_src);
	if (rt->rt_dst != rt->rt_gateway)
		RTA_PUT(skb, RTA_GATEWAY, 4, &rt->rt_gateway);
	mx = (struct rtattr*)skb->tail;
	RTA_PUT(skb, RTA_METRICS, 0, NULL);
	if (rt->u.dst.mxlock)
		RTA_PUT(skb, RTAX_LOCK, sizeof(unsigned), &rt->u.dst.mxlock);
	if (rt->u.dst.pmtu)
		RTA_PUT(skb, RTAX_MTU, sizeof(unsigned), &rt->u.dst.pmtu);
	if (rt->u.dst.window)
		RTA_PUT(skb, RTAX_WINDOW, sizeof(unsigned), &rt->u.dst.window);
	if (rt->u.dst.rtt)
		RTA_PUT(skb, RTAX_RTT, sizeof(unsigned), &rt->u.dst.rtt);
	mx->rta_len = skb->tail - (u8*)mx;
	if (mx->rta_len == RTA_LENGTH(0))
		skb_trim(skb, (u8*)mx - skb->data);
	ci.rta_lastuse = jiffies - rt->u.dst.lastuse;
	ci.rta_used = atomic_read(&rt->u.dst.refcnt);
	ci.rta_clntref = atomic_read(&rt->u.dst.use);
	if (rt->u.dst.expires)
		ci.rta_expires = rt->u.dst.expires - jiffies;
	else
		ci.rta_expires = 0;
	ci.rta_error = rt->u.dst.error;
#ifdef CONFIG_IP_MROUTE
	eptr = (struct rtattr*)skb->tail;
#endif
	RTA_PUT(skb, RTA_CACHEINFO, sizeof(ci), &ci);
	if (rt->key.iif) {
#ifdef CONFIG_IP_MROUTE
		u32 dst = rt->rt_dst;

		if (MULTICAST(dst) && !LOCAL_MCAST(dst) && ipv4_devconf.mc_forwarding) {
			int err = ipmr_get_route(skb, r, nowait);
			if (err <= 0) {
				if (!nowait) {
					if (err == 0)
						return 0;
					goto nlmsg_failure;
				} else {
					if (err == -EMSGSIZE)
						goto nlmsg_failure;
					((struct rta_cacheinfo*)RTA_DATA(eptr))->rta_error = err;
				}
			}
		} else
#endif
		{
			RTA_PUT(skb, RTA_IIF, sizeof(int), &rt->key.iif);
		}
	}

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

int inet_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct rtable *rt = NULL;
	u32 dst = 0;
	u32 src = 0;
	int iif = 0;
	int err;
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL)
		return -ENOBUFS;

	/* Reserve room for dummy headers, this skb can pass
	   through good chunk of routing engine.
	 */
	skb->mac.raw = skb->data;
	skb_reserve(skb, MAX_HEADER + sizeof(struct iphdr));

	if (rta[RTA_SRC-1])
		memcpy(&src, RTA_DATA(rta[RTA_SRC-1]), 4);
	if (rta[RTA_DST-1])
		memcpy(&dst, RTA_DATA(rta[RTA_DST-1]), 4);
	if (rta[RTA_IIF-1])
		memcpy(&iif, RTA_DATA(rta[RTA_IIF-1]), sizeof(int));

	if (iif) {
		struct device *dev;
		dev = dev_get_by_index(iif);
		if (!dev)
			return -ENODEV;
		skb->protocol = __constant_htons(ETH_P_IP);
		skb->dev = dev;
		start_bh_atomic();
		err = ip_route_input(skb, dst, src, rtm->rtm_tos, dev);
		end_bh_atomic();
		rt = (struct rtable*)skb->dst;
		if (!err && rt->u.dst.error)
			err = -rt->u.dst.error;
	} else {
		int oif = 0;
		if (rta[RTA_OIF-1])
			memcpy(&oif, RTA_DATA(rta[RTA_OIF-1]), sizeof(int));
		err = ip_route_output(&rt, dst, src, rtm->rtm_tos, oif);
	}
	if (err) {
		kfree_skb(skb);
		return err;
	}

	skb->dst = &rt->u.dst;
	if (rtm->rtm_flags & RTM_F_NOTIFY)
		rt->rt_flags |= RTCF_NOTIFY;

	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;

	err = rt_fill_info(skb, NETLINK_CB(in_skb).pid, nlh->nlmsg_seq, RTM_NEWROUTE, 0);
	if (err == 0)
		return 0;
	if (err < 0)
		return -EMSGSIZE;

	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
	if (err < 0)
		return err;
	return 0;
}


int ip_rt_dump(struct sk_buff *skb,  struct netlink_callback *cb)
{
	struct rtable *rt;
	int h, s_h;
	int idx, s_idx;

	s_h = cb->args[0];
	s_idx = idx = cb->args[1];
	for (h=0; h < RT_HASH_DIVISOR; h++) {
		if (h < s_h) continue;
		if (h > s_h)
			s_idx = 0;
		start_bh_atomic();
		for (rt = rt_hash_table[h], idx = 0; rt; rt = rt->u.rt_next, idx++) {
			if (idx < s_idx)
				continue;
			skb->dst = dst_clone(&rt->u.dst);
			if (rt_fill_info(skb, NETLINK_CB(cb->skb).pid,
					 cb->nlh->nlmsg_seq, RTM_NEWROUTE, 1) <= 0) {
				dst_release(xchg(&skb->dst, NULL));
				end_bh_atomic();
				goto done;
			}
			dst_release(xchg(&skb->dst, NULL));
		}
		end_bh_atomic();
	}

done:
	cb->args[0] = h;
	cb->args[1] = idx;
	return skb->len;
}

#endif /* CONFIG_RTNETLINK */

void ip_rt_multicast_event(struct in_device *in_dev)
{
	rt_cache_flush(0);
}



#ifdef CONFIG_SYSCTL

static int flush_delay;

static
int ipv4_sysctl_rtcache_flush(ctl_table *ctl, int write, struct file * filp,
			      void *buffer, size_t *lenp)
{
	if (write) {
		proc_dointvec(ctl, write, filp, buffer, lenp);
		rt_cache_flush(flush_delay);
		return 0;
	} else
		return -EINVAL;
}

static int ipv4_sysctl_rtcache_flush_strategy(ctl_table *table, int *name, int nlen,
			 void *oldval, size_t *oldlenp,
			 void *newval, size_t newlen, 
			 void **context)
{
	int delay;
	if (newlen != sizeof(int))
		return -EINVAL;
	if (get_user(delay,(int *)newval))
		return -EFAULT; 
	rt_cache_flush(delay); 
	return 0;
}

ctl_table ipv4_route_table[] = {
        {NET_IPV4_ROUTE_FLUSH, "flush",
         &flush_delay, sizeof(int), 0644, NULL,
         &ipv4_sysctl_rtcache_flush, &ipv4_sysctl_rtcache_flush_strategy },
	{NET_IPV4_ROUTE_MIN_DELAY, "min_delay",
         &ip_rt_min_delay, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_IPV4_ROUTE_MAX_DELAY, "max_delay",
         &ip_rt_max_delay, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_IPV4_ROUTE_GC_THRESH, "gc_thresh",
         &ipv4_dst_ops.gc_thresh, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_MAX_SIZE, "max_size",
         &ip_rt_max_size, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_GC_MIN_INTERVAL, "gc_min_interval",
         &ip_rt_gc_min_interval, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_IPV4_ROUTE_GC_TIMEOUT, "gc_timeout",
         &ip_rt_gc_timeout, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_IPV4_ROUTE_GC_INTERVAL, "gc_interval",
         &ip_rt_gc_interval, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	{NET_IPV4_ROUTE_REDIRECT_LOAD, "redirect_load",
         &ip_rt_redirect_load, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_REDIRECT_NUMBER, "redirect_number",
         &ip_rt_redirect_number, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_REDIRECT_SILENCE, "redirect_silence",
         &ip_rt_redirect_silence, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_ERROR_COST, "error_cost",
         &ip_rt_error_cost, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_ERROR_BURST, "error_burst",
         &ip_rt_error_burst, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_GC_ELASTICITY, "gc_elasticity",
         &ip_rt_gc_elasticity, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_ROUTE_MTU_EXPIRES, "mtu_expires",
         &ip_rt_mtu_expires, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies, &sysctl_jiffies},
	 {0}
};
#endif

#ifdef CONFIG_NET_CLS_ROUTE
struct ip_rt_acct ip_rt_acct[256];

#ifdef CONFIG_PROC_FS
static int ip_rt_acct_read(char *buffer, char **start, off_t offset,
			   int length, int *eof, void *data)
{
	*start=buffer;

	if (offset + length > sizeof(ip_rt_acct)) {
		length = sizeof(ip_rt_acct) - offset;
		*eof = 1;
	}
	if (length > 0) {
		start_bh_atomic();
		memcpy(buffer, ((u8*)&ip_rt_acct)+offset, length);
		end_bh_atomic();
		return length;
	}
	return 0;
}
#endif
#endif


__initfunc(void ip_rt_init(void))
{
#ifdef CONFIG_PROC_FS
#ifdef CONFIG_NET_CLS_ROUTE
	struct proc_dir_entry *ent;
#endif
#endif
	devinet_init();
	ip_fib_init();
	rt_periodic_timer.function = rt_check_expire;
	/* All the timers, started at system startup tend
	   to synchronize. Perturb it a bit.
	 */
	rt_periodic_timer.expires = jiffies + net_random()%ip_rt_gc_interval
		+ ip_rt_gc_interval;
	add_timer(&rt_periodic_timer);

#ifdef CONFIG_PROC_FS
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_RTCACHE, 8, "rt_cache",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		rt_cache_get_info
	});
#ifdef CONFIG_NET_CLS_ROUTE
	ent = create_proc_entry("net/rt_acct", 0, 0);
	ent->read_proc = ip_rt_acct_read;
#endif
#endif
}
