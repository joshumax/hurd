#ifndef _NET_NEIGHBOUR_H
#define _NET_NEIGHBOUR_H

/*
 *	Generic neighbour manipulation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 */

/* The following flags & states are exported to user space,
   so that they should be moved to include/linux/ directory.
 */

/*
 *	Neighbor Cache Entry Flags
 */

#define NTF_PROXY	0x08	/* == ATF_PUBL */
#define NTF_ROUTER	0x80

/*
 *	Neighbor Cache Entry States.
 */

#define NUD_INCOMPLETE	0x01
#define NUD_REACHABLE	0x02
#define NUD_STALE	0x04
#define NUD_DELAY	0x08
#define NUD_PROBE	0x10
#define NUD_FAILED	0x20

/* Dummy states */
#define NUD_NOARP	0x40
#define NUD_PERMANENT	0x80
#define NUD_NONE	0x00

/* NUD_NOARP & NUD_PERMANENT are pseudostates, they never change
   and make no address resolution or NUD.
   NUD_PERMANENT is also cannot be deleted by garbage collectors.
 */

#ifdef __KERNEL__

#include <asm/atomic.h>
#include <linux/skbuff.h>

#define NUD_IN_TIMER	(NUD_INCOMPLETE|NUD_DELAY|NUD_PROBE)
#define NUD_VALID	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE|NUD_PROBE|NUD_STALE|NUD_DELAY)
#define NUD_CONNECTED	(NUD_PERMANENT|NUD_NOARP|NUD_REACHABLE)

struct neigh_parms
{
	struct neigh_parms *next;
	int	(*neigh_setup)(struct neighbour *);
	struct neigh_table *tbl;
	int	entries;
	void	*priv;

	void	*sysctl_table;

	int	base_reachable_time;
	int	retrans_time;
	int	gc_staletime;
	int	reachable_time;
	int	delay_probe_time;

	int	queue_len;
	int	ucast_probes;
	int	app_probes;
	int	mcast_probes;
	int	anycast_delay;
	int	proxy_delay;
	int	proxy_qlen;
	int	locktime;
};

struct neigh_statistics
{
	unsigned long allocs;
	unsigned long res_failed;
	unsigned long rcv_probes_mcast;
	unsigned long rcv_probes_ucast;
};

struct neighbour
{
	struct neighbour	*next;
	struct neigh_table	*tbl;
	struct neigh_parms	*parms;
	struct device		*dev;
	unsigned long		used;
	unsigned long		confirmed;
	unsigned long		updated;
	__u8			flags;
	__u8			nud_state;
	__u8			type;
	__u8			probes;
	unsigned char		ha[MAX_ADDR_LEN];
	struct hh_cache		*hh;
	atomic_t		refcnt;
	int			(*output)(struct sk_buff *skb);
	struct sk_buff_head	arp_queue;
	struct timer_list	timer;
	struct neigh_ops	*ops;
	u8			primary_key[0];
};

struct neigh_ops
{
	int			family;
	void			(*destructor)(struct neighbour *);
	void			(*solicit)(struct neighbour *, struct sk_buff*);
	void			(*error_report)(struct neighbour *, struct sk_buff*);
	int			(*output)(struct sk_buff*);
	int			(*connected_output)(struct sk_buff*);
	int			(*hh_output)(struct sk_buff*);
	int			(*queue_xmit)(struct sk_buff*);
};

struct pneigh_entry
{
	struct pneigh_entry	*next;
	struct device		*dev;
	u8			key[0];
};

#define NEIGH_HASHMASK		0x1F
#define PNEIGH_HASHMASK		0xF

/*
 *	neighbour table manipulation
 */


struct neigh_table
{
	struct neigh_table	*next;
	int			family;
	int			entry_size;
	int			key_len;
	int			(*constructor)(struct neighbour *);
	int			(*pconstructor)(struct pneigh_entry *);
	void			(*pdestructor)(struct pneigh_entry *);
	void			(*proxy_redo)(struct sk_buff *skb);
	struct neigh_parms	parms;
	/* HACK. gc_* shoul follow parms without a gap! */
	int			gc_interval;
	int			gc_thresh1;
	int			gc_thresh2;
	int			gc_thresh3;
	unsigned long		last_flush;
	struct timer_list 	gc_timer;
	struct timer_list 	proxy_timer;
	struct sk_buff_head	proxy_queue;
	int			entries;
	atomic_t		lock;
	unsigned long		last_rand;
	struct neigh_parms	*parms_list;
	struct neigh_statistics	stats;
	struct neighbour	*hash_buckets[NEIGH_HASHMASK+1];
	struct pneigh_entry	*phash_buckets[PNEIGH_HASHMASK+1];
};

extern void			neigh_table_init(struct neigh_table *tbl);
extern int			neigh_table_clear(struct neigh_table *tbl);
extern struct neighbour		*__neigh_lookup(struct neigh_table *tbl,
					       const void *pkey, struct device *dev,
					       int creat);
extern void			neigh_destroy(struct neighbour *neigh);
extern int			__neigh_event_send(struct neighbour *neigh, struct sk_buff *skb);
extern int			neigh_update(struct neighbour *neigh, u8 *lladdr, u8 new, int override, int arp);
extern int			neigh_ifdown(struct neigh_table *tbl, struct device *dev);
extern int			neigh_resolve_output(struct sk_buff *skb);
extern int			neigh_connected_output(struct sk_buff *skb);
extern int			neigh_compat_output(struct sk_buff *skb);
extern struct neighbour 	*neigh_event_ns(struct neigh_table *tbl,
						u8 *lladdr, void *saddr,
						struct device *dev);

extern struct neigh_parms	*neigh_parms_alloc(struct device *dev, struct neigh_table *tbl);
extern void			neigh_parms_release(struct neigh_table *tbl, struct neigh_parms *parms);
extern unsigned long		neigh_rand_reach_time(unsigned long base);

extern void			pneigh_enqueue(struct neigh_table *tbl, struct neigh_parms *p,
					       struct sk_buff *skb);
extern struct pneigh_entry	*pneigh_lookup(struct neigh_table *tbl, const void *key, struct device *dev, int creat);
extern int			pneigh_delete(struct neigh_table *tbl, const void *key, struct device *dev);

struct netlink_callback;
struct nlmsghdr;
extern int neigh_dump_info(struct sk_buff *skb, struct netlink_callback *cb);
extern int neigh_add(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern int neigh_delete(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg);
extern void neigh_app_ns(struct neighbour *n);

extern int			neigh_sysctl_register(struct device *dev, struct neigh_parms *p,
						      int p_id, int pdev_id, char *p_name);
extern void			neigh_sysctl_unregister(struct neigh_parms *p);

/*
 *	Neighbour references
 *
 *	When neighbour pointers are passed to "client" code the
 *	reference count is increased. The count is 0 if the node
 *	is only referenced by the corresponding table.
 */

static __inline__ void neigh_release(struct neighbour *neigh)
{
	if (atomic_dec_and_test(&neigh->refcnt) && neigh->tbl == NULL)
		neigh_destroy(neigh);
}

static __inline__ struct neighbour * neigh_clone(struct neighbour *neigh)
{
	if (neigh)
		atomic_inc(&neigh->refcnt);
	return neigh;
}

static __inline__ void neigh_confirm(struct neighbour *neigh)
{
	if (neigh)
		neigh->confirmed = jiffies;
}

static __inline__ struct neighbour *
neigh_lookup(struct neigh_table *tbl, const void *pkey, struct device *dev)
{
	struct neighbour *neigh;
	start_bh_atomic();
	neigh = __neigh_lookup(tbl, pkey, dev, 0);
	end_bh_atomic();
	return neigh;
}

static __inline__ int neigh_is_connected(struct neighbour *neigh)
{
	return neigh->nud_state&NUD_CONNECTED;
}

static __inline__ int neigh_is_valid(struct neighbour *neigh)
{
	return neigh->nud_state&NUD_VALID;
}

static __inline__ int neigh_event_send(struct neighbour *neigh, struct sk_buff *skb)
{
	neigh->used = jiffies;
	if (!(neigh->nud_state&(NUD_CONNECTED|NUD_DELAY|NUD_PROBE)))
		return __neigh_event_send(neigh, skb);
	return 0;
}

static __inline__ void neigh_table_lock(struct neigh_table *tbl)
{
	atomic_inc(&tbl->lock);
	synchronize_bh();
}

static __inline__ void neigh_table_unlock(struct neigh_table *tbl)
{
	atomic_dec(&tbl->lock);
}


#endif
#endif
