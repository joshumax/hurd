/*
 * net/dst.c	Protocol independent destination cache.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <net/dst.h>

struct dst_entry * dst_garbage_list;
atomic_t	dst_total = ATOMIC_INIT(0);

static unsigned long dst_gc_timer_expires;
static unsigned long dst_gc_timer_inc = DST_GC_MAX;
static void dst_run_gc(unsigned long);

static struct timer_list dst_gc_timer =
	{ NULL, NULL, DST_GC_MIN, 0L, dst_run_gc };

#if RT_CACHE_DEBUG >= 2
atomic_t hh_count;
#endif

static void dst_run_gc(unsigned long dummy)
{
	int    delayed = 0;
	struct dst_entry * dst, **dstp;

	del_timer(&dst_gc_timer);
	dstp = &dst_garbage_list;
	while ((dst = *dstp) != NULL) {
		if (atomic_read(&dst->use)) {
			dstp = &dst->next;
			delayed++;
			continue;
		}
		*dstp = dst->next;
		dst_destroy(dst);
	}
	if (!dst_garbage_list) {
		dst_gc_timer_inc = DST_GC_MAX;
		return;
	}
	if ((dst_gc_timer_expires += dst_gc_timer_inc) > DST_GC_MAX)
		dst_gc_timer_expires = DST_GC_MAX;
	dst_gc_timer_inc += DST_GC_INC;
	dst_gc_timer.expires = jiffies + dst_gc_timer_expires;
#if RT_CACHE_DEBUG >= 2
	printk("dst_total: %d/%d %ld\n",
	       atomic_read(&dst_total), delayed,  dst_gc_timer_expires);
#endif
	add_timer(&dst_gc_timer);
}

static int dst_discard(struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

static int dst_blackhole(struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

void * dst_alloc(int size, struct dst_ops * ops)
{
	struct dst_entry * dst;

	if (ops->gc && atomic_read(&ops->entries) > ops->gc_thresh) {
		if (ops->gc())
			return NULL;
	}
	dst = kmalloc(size, GFP_ATOMIC);
	if (!dst)
		return NULL;
	memset(dst, 0, size);
	dst->ops = ops;
	atomic_set(&dst->refcnt, 0);
	dst->lastuse = jiffies;
	dst->input = dst_discard;
	dst->output = dst_blackhole;
	atomic_inc(&dst_total);
	atomic_inc(&ops->entries);
	return dst;
}

void __dst_free(struct dst_entry * dst)
{
	start_bh_atomic();
	/* The first case (dev==NULL) is required, when
	   protocol module is unloaded.
	 */
	if (dst->dev == NULL || !(dst->dev->flags&IFF_UP)) {
		dst->input = dst_discard;
		dst->output = dst_blackhole;
		dst->dev = &loopback_dev;
	}
	dst->obsolete = 2;
	dst->next = dst_garbage_list;
	dst_garbage_list = dst;
	if (dst_gc_timer_inc > DST_GC_INC) {
		del_timer(&dst_gc_timer);
		dst_gc_timer_inc = DST_GC_INC;
		dst_gc_timer_expires = DST_GC_MIN;
		dst_gc_timer.expires = jiffies + dst_gc_timer_expires;
		add_timer(&dst_gc_timer);
	}
	end_bh_atomic();
}

void dst_destroy(struct dst_entry * dst)
{
	struct neighbour *neigh = dst->neighbour;
	struct hh_cache *hh = dst->hh;

	dst->hh = NULL;
	if (hh && atomic_dec_and_test(&hh->hh_refcnt))
		kfree(hh);

	if (neigh) {
		dst->neighbour = NULL;
		neigh_release(neigh);
	}

	atomic_dec(&dst->ops->entries);

	if (dst->ops->destroy)
		dst->ops->destroy(dst);
	atomic_dec(&dst_total);
	kfree(dst);
}
