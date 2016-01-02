/*
 *	IPv6 fragment reassembly
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: reassembly.c,v 1.1 2007/10/08 21:12:31 stesie Exp $
 *
 *	Based on: net/ipv4/ip_fragment.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/* 
 *	Fixes:	
 *	Andi Kleen	Make it work with multiple hosts.
 *			More RFC compliance.
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>

int sysctl_ip6frag_high_thresh = 256*1024;
int sysctl_ip6frag_low_thresh = 192*1024;
int sysctl_ip6frag_time = IPV6_FRAG_TIMEOUT;

atomic_t ip6_frag_mem = ATOMIC_INIT(0);

struct ipv6_frag {
	__u16			offset;
	__u16			len;
	struct sk_buff		*skb;

	struct frag_hdr		*fhdr;

	struct ipv6_frag	*next;
};

/*
 *	Equivalent of ipv4 struct ipq
 */

struct frag_queue {

	struct frag_queue	*next;
	struct frag_queue	*prev;

	__u32			id;		/* fragment id		*/
	struct in6_addr		saddr;
	struct in6_addr		daddr;
	struct timer_list	timer;		/* expire timer		*/
	struct ipv6_frag	*fragments;
	struct device		*dev;
	int			iif;
	__u8			last_in;	/* has first/last segment arrived? */
#define FIRST_IN		2
#define LAST_IN			1
	__u8			nexthdr;
	__u16			nhoffset;
};

static struct frag_queue ipv6_frag_queue = {
	&ipv6_frag_queue, &ipv6_frag_queue,
	0, {{{0}}}, {{{0}}},
	{0}, NULL, NULL,
	0, 0, 0, 0
};

/* Memory Tracking Functions. */
static __inline__ void frag_kfree_skb(struct sk_buff *skb)
{
	atomic_sub(skb->truesize, &ip6_frag_mem);
	kfree_skb(skb);
}

static __inline__ void frag_kfree_s(void *ptr, int len)
{
	atomic_sub(len, &ip6_frag_mem);
	kfree(ptr);
}
 
static __inline__ void *frag_kmalloc(int size, int pri)
{
	void *vp = kmalloc(size, pri);

	if(!vp)
		return NULL;
	atomic_add(size, &ip6_frag_mem);
	return vp;
}


static void			create_frag_entry(struct sk_buff *skb, 
						  __u8 *nhptr,
						  struct frag_hdr *fhdr);
static u8 *			reasm_frag(struct frag_queue *fq, 
					   struct sk_buff **skb_in);

static void			reasm_queue(struct frag_queue *fq, 
					    struct sk_buff *skb, 
					    struct frag_hdr *fhdr,
					    u8 *nhptr);

static void			fq_free(struct frag_queue *fq);

static void frag_prune(void)
{
	struct frag_queue *fq;

	while ((fq = ipv6_frag_queue.next) != &ipv6_frag_queue) {
		ipv6_statistics.Ip6ReasmFails++;
		fq_free(fq);
		if (atomic_read(&ip6_frag_mem) <= sysctl_ip6frag_low_thresh)
			return;
	}
	if (atomic_read(&ip6_frag_mem))
		printk(KERN_DEBUG "IPv6 frag_prune: memleak\n");
	atomic_set(&ip6_frag_mem, 0);
}


u8* ipv6_reassembly(struct sk_buff **skbp, __u8 *nhptr)
{
	struct sk_buff *skb = *skbp; 
	struct frag_hdr *fhdr = (struct frag_hdr *) (skb->h.raw);
	struct frag_queue *fq;
	struct ipv6hdr *hdr;

	hdr = skb->nh.ipv6h;

	ipv6_statistics.Ip6ReasmReqds++;

	/* Jumbo payload inhibits frag. header */
	if (hdr->payload_len==0) {
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, skb->h.raw);
		return NULL;
	}
	if ((u8 *)(fhdr+1) > skb->tail) {
		icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, skb->h.raw);
		return NULL;
	}
	if (atomic_read(&ip6_frag_mem) > sysctl_ip6frag_high_thresh)
		frag_prune();

	for (fq = ipv6_frag_queue.next; fq != &ipv6_frag_queue; fq = fq->next) {
		if (fq->id == fhdr->identification && 
		    !ipv6_addr_cmp(&hdr->saddr, &fq->saddr) &&
		    !ipv6_addr_cmp(&hdr->daddr, &fq->daddr)) {

			reasm_queue(fq, skb, fhdr, nhptr);

			if (fq->last_in == (FIRST_IN|LAST_IN))
				return reasm_frag(fq, skbp);

			return NULL;
		}
	}

	create_frag_entry(skb, nhptr, fhdr);

	return NULL;
}


static void fq_free(struct frag_queue *fq)
{
	struct ipv6_frag *fp, *back;

	del_timer(&fq->timer);

	for (fp = fq->fragments; fp; ) {
		frag_kfree_skb(fp->skb);
		back = fp;
		fp=fp->next;
		frag_kfree_s(back, sizeof(*back));
	}

	fq->prev->next = fq->next;
	fq->next->prev = fq->prev;

	fq->prev = fq->next = NULL;
	
	frag_kfree_s(fq, sizeof(*fq));
}

static void frag_expire(unsigned long data)
{
	struct frag_queue *fq;
	struct ipv6_frag *frag;

	fq = (struct frag_queue *) data;

	frag = fq->fragments;

	ipv6_statistics.Ip6ReasmTimeout++;
	ipv6_statistics.Ip6ReasmFails++;

	if (frag == NULL) {
		printk(KERN_DEBUG "invalid fragment queue\n");
		return;
	}

	/* Send error only if the first segment arrived.
	   (fixed --ANK (980728))
	 */
	if (fq->last_in&FIRST_IN) {
		struct device *dev = dev_get_by_index(fq->iif);

		/*
		   But use as source device on which LAST ARRIVED
		   segment was received. And do not use fq->dev
		   pointer directly, device might already disappeared.
		 */
		if (dev) {
			frag->skb->dev = dev;
			icmpv6_send(frag->skb, ICMPV6_TIME_EXCEED, ICMPV6_EXC_FRAGTIME, 0,
				    dev);
		}
	}
	
	fq_free(fq);
}


static void create_frag_entry(struct sk_buff *skb,
			      __u8 *nhptr,
			      struct frag_hdr *fhdr)
{
	struct frag_queue *fq;
	struct ipv6hdr *hdr; 

	fq = (struct frag_queue *) frag_kmalloc(sizeof(struct frag_queue), 
						GFP_ATOMIC);

	if (fq == NULL) {
		ipv6_statistics.Ip6ReasmFails++;
		kfree_skb(skb);
		return;
	}

	memset(fq, 0, sizeof(struct frag_queue));

	fq->id = fhdr->identification;

	hdr = skb->nh.ipv6h;
	ipv6_addr_copy(&fq->saddr, &hdr->saddr);
	ipv6_addr_copy(&fq->daddr, &hdr->daddr);

	/* init_timer has been done by the memset */
	fq->timer.function = frag_expire;
	fq->timer.data = (long) fq;
	fq->timer.expires = jiffies + sysctl_ip6frag_time;

	reasm_queue(fq, skb, fhdr, nhptr);

	if (fq->fragments) {
		fq->prev = ipv6_frag_queue.prev;
		fq->next = &ipv6_frag_queue;
		fq->prev->next = fq;
		ipv6_frag_queue.prev = fq;

		add_timer(&fq->timer);
	} else
		frag_kfree_s(fq, sizeof(*fq));
}


/*
 *	We queue the packet even if it's the last.
 *	It's a trade off. This allows the reassembly 
 *	code to be simpler (=faster) and of the
 *	steps we do for queueing the only unnecessary 
 *	one it's the kmalloc for a struct ipv6_frag.
 *	Feel free to try other alternatives...
 */

static void reasm_queue(struct frag_queue *fq, struct sk_buff *skb, 
				     struct frag_hdr *fhdr, u8 *nhptr)
{
	struct ipv6_frag *nfp, *fp, **bptr;

	nfp = (struct ipv6_frag *) frag_kmalloc(sizeof(struct ipv6_frag), 
						GFP_ATOMIC);

	if (nfp == NULL) {		
		kfree_skb(skb);
		return;
	}

	nfp->offset = ntohs(fhdr->frag_off) & ~0x7;
	nfp->len = (ntohs(skb->nh.ipv6h->payload_len) -
		    ((u8 *) (fhdr + 1) - (u8 *) (skb->nh.ipv6h + 1)));

	if ((u32)nfp->offset + (u32)nfp->len >= 65536) {
		icmpv6_param_prob(skb,ICMPV6_HDR_FIELD, (u8*)&fhdr->frag_off); 
		goto err;
	}
	if (fhdr->frag_off & __constant_htons(0x0001)) {
		/* Check if the fragment is rounded to 8 bytes.
		 * Required by the RFC.
		 * ... and would break our defragmentation algorithm 8)
		 */
		if (nfp->len & 0x7) {
			printk(KERN_DEBUG "fragment not rounded to 8bytes\n");

			/*
			   It is not in specs, but I see no reasons
			   to send an error in this case. --ANK
			 */
			if (nfp->offset == 0)
				icmpv6_param_prob(skb, ICMPV6_HDR_FIELD, 
						  &skb->nh.ipv6h->payload_len);
			goto err;
		}
	}

	nfp->skb  = skb;
	nfp->fhdr = fhdr;
	nfp->next = NULL;

	bptr = &fq->fragments;

	for (fp = fq->fragments; fp; fp=fp->next) {
		if (nfp->offset <= fp->offset)
			break;
		bptr = &fp->next;
	}
	if (fp && fp->offset == nfp->offset) {
		if (nfp->len != fp->len) {
			printk(KERN_DEBUG "reasm_queue: dup with wrong len\n");
		}

		/* duplicate. discard it. */
		goto err;
	}

	atomic_add(skb->truesize, &ip6_frag_mem);

	/* All the checks are done, fragment is acepted.
	   Only now we are allowed to update reassembly data!
	   (fixed --ANK (980728))
	 */

	/* iif always set to one of the last arrived segment */
	fq->dev = skb->dev;
	fq->iif = skb->dev->ifindex;

	/* Last fragment */
	if ((fhdr->frag_off & __constant_htons(0x0001)) == 0)
		fq->last_in |= LAST_IN;

	/* First fragment.
	   nexthdr and nhptr are get from the first fragment.
	   Moreover, nexthdr is UNDEFINED for all the fragments but the
	   first one.
	   (fixed --ANK (980728))
	 */
	if (nfp->offset == 0) {
		fq->nexthdr = fhdr->nexthdr;
		fq->last_in |= FIRST_IN;
		fq->nhoffset = nhptr - skb->nh.raw;
	}

	*bptr = nfp;
	nfp->next = fp;
	return;

err:
	frag_kfree_s(nfp, sizeof(*nfp));
	kfree_skb(skb);
}

/*
 *	check if this fragment completes the packet
 *	returns true on success
 */
static u8* reasm_frag(struct frag_queue *fq, struct sk_buff **skb_in)
{
	struct ipv6_frag *fp;
	struct ipv6_frag *head = fq->fragments;
	struct ipv6_frag *tail = NULL;
	struct sk_buff *skb;
	__u32  offset = 0;
	__u32  payload_len;
	__u16  unfrag_len;
	__u16  copy;
	u8     *nhptr;

	for(fp = head; fp; fp=fp->next) {
		if (offset != fp->offset)
			return NULL;

		offset += fp->len;
		tail = fp;
	}

	/* 
	 * we know the m_flag arrived and we have a queue,
	 * starting from 0, without gaps.
	 * this means we have all fragments.
	 */

	/* Unfragmented part is taken from the first segment.
	   (fixed --ANK (980728))
	 */
	unfrag_len = (u8 *) (head->fhdr) - (u8 *) (head->skb->nh.ipv6h + 1);

	payload_len = (unfrag_len + tail->offset + 
		       (tail->skb->tail - (__u8 *) (tail->fhdr + 1)));

	if (payload_len > 65535) {
		if (net_ratelimit())
			printk(KERN_DEBUG "reasm_frag: payload len = %d\n", payload_len);
		ipv6_statistics.Ip6ReasmFails++;
		fq_free(fq);
		return NULL;
	}

	if ((skb = dev_alloc_skb(sizeof(struct ipv6hdr) + payload_len))==NULL) {
		if (net_ratelimit())
			printk(KERN_DEBUG "reasm_frag: no memory for reassembly\n");
		ipv6_statistics.Ip6ReasmFails++;
		fq_free(fq);
		return NULL;
	}

	copy = unfrag_len + sizeof(struct ipv6hdr);

	skb->nh.ipv6h = (struct ipv6hdr *) skb->data;
	skb->dev = fq->dev;
	skb->protocol = __constant_htons(ETH_P_IPV6);
	skb->pkt_type = head->skb->pkt_type;
	memcpy(skb->cb, head->skb->cb, sizeof(skb->cb));
	skb->dst = dst_clone(head->skb->dst);

	memcpy(skb_put(skb, copy), head->skb->nh.ipv6h, copy);
	nhptr = skb->nh.raw + fq->nhoffset;
	*nhptr = fq->nexthdr;

	skb->h.raw = skb->tail;

	skb->nh.ipv6h->payload_len = ntohs(payload_len);

	*skb_in = skb;

	/*
	 *	FIXME: If we don't have a checksum we ought to be able
	 *	to defragment and checksum in this pass. [AC]
	 *	Note that we don't really know yet whether the protocol
	 *	needs checksums at all. It might still be a good idea. -AK
	 */
	for(fp = fq->fragments; fp; ) {
		struct ipv6_frag *back;

		memcpy(skb_put(skb, fp->len), (__u8*)(fp->fhdr + 1), fp->len);
		frag_kfree_skb(fp->skb);
		back = fp;
		fp=fp->next;
		frag_kfree_s(back, sizeof(*back));
	}

	del_timer(&fq->timer);
	fq->prev->next = fq->next;
	fq->next->prev = fq->prev;
	fq->prev = fq->next = NULL;

	frag_kfree_s(fq, sizeof(*fq));

	ipv6_statistics.Ip6ReasmOKs++;
	return nhptr;
}
