/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *		
 * Version:	$Id: ip_fragment.c,v 1.40 1999/03/20 23:58:34 davem Exp $
 *
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 *		David S. Miller :	Begin massive cleanup...
 *		Andi Kleen	:	Add sysctls.
 *		xxxx		:	Overlapfrag bug.
 *		Ultima          :       ip_expire() kernel panic.
 *		Bill Hawes	:	Frag accounting and evictor fixes.
 *		John McDonald	:	0 length frag bug.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>

/* Fragment cache limits. We will commit 256K at one time. Should we
 * cross that limit we will prune down to 192K. This should cope with
 * even the most extreme cases without allowing an attacker to measurably
 * harm machine performance.
 */
int sysctl_ipfrag_high_thresh = 256*1024;
int sysctl_ipfrag_low_thresh = 192*1024;

int sysctl_ipfrag_time = IP_FRAG_TIME;

/* Describe an IP fragment. */
struct ipfrag {
	int		offset;		/* offset of fragment in IP datagram	*/
	int		end;		/* last byte of data in datagram	*/
	int		len;		/* length of this fragment		*/
	struct sk_buff	*skb;		/* complete received fragment		*/
	unsigned char	*ptr;		/* pointer into real fragment data	*/
	struct ipfrag	*next;		/* linked list pointers			*/
	struct ipfrag	*prev;
};

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq {
	struct iphdr	*iph;		/* pointer to IP header			*/
	struct ipq	*next;		/* linked list pointers			*/
	struct ipfrag	*fragments;	/* linked list of received fragments	*/
	int		len;		/* total length of original datagram	*/
	short		ihlen;		/* length of the IP header		*/	
	struct timer_list timer;	/* when will this queue expire?		*/
	struct ipq	**pprev;
	struct device	*dev;		/* Device - for icmp replies */
};

#define IPQ_HASHSZ	64

struct ipq *ipq_hash[IPQ_HASHSZ];

#define ipqhashfn(id, saddr, daddr, prot) \
	((((id) >> 1) ^ (saddr) ^ (daddr) ^ (prot)) & (IPQ_HASHSZ - 1))

atomic_t ip_frag_mem = ATOMIC_INIT(0);		/* Memory used for fragments */

/* Memory Tracking Functions. */
static __inline__ void frag_kfree_skb(struct sk_buff *skb)
{
	atomic_sub(skb->truesize, &ip_frag_mem);
	kfree_skb(skb);
}

static __inline__ void frag_kfree_s(void *ptr, int len)
{
	atomic_sub(len, &ip_frag_mem);
	kfree(ptr);
}
 
static __inline__ void *frag_kmalloc(int size, int pri)
{
	void *vp = kmalloc(size, pri);

	if(!vp)
		return NULL;
	atomic_add(size, &ip_frag_mem);
	return vp;
}
 
/* Create a new fragment entry. */
static struct ipfrag *ip_frag_create(int offset, int end,
				     struct sk_buff *skb, unsigned char *ptr)
{
	struct ipfrag *fp;

	fp = (struct ipfrag *) frag_kmalloc(sizeof(struct ipfrag), GFP_ATOMIC);
	if (fp == NULL)
		goto out_nomem;

	/* Fill in the structure. */
	fp->offset = offset;
	fp->end = end;
	fp->len = end - offset;
	fp->skb = skb;
	fp->ptr = ptr;
	fp->next = fp->prev = NULL;
	
	/* Charge for the SKB as well. */
	atomic_add(skb->truesize, &ip_frag_mem);

	return(fp);

out_nomem:
	NETDEBUG(printk(KERN_ERR "IP: frag_create: no memory left !\n"));
	return(NULL);
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and return the queue entry address if found.
 */
static inline struct ipq *ip_find(struct iphdr *iph, struct dst_entry *dst)
{
	__u16 id = iph->id;
	__u32 saddr = iph->saddr;
	__u32 daddr = iph->daddr;
	__u8 protocol = iph->protocol;
	unsigned int hash = ipqhashfn(id, saddr, daddr, protocol);
	struct ipq *qp;

	/* Always, we are in a BH context, so no locking.  -DaveM */
	for(qp = ipq_hash[hash]; qp; qp = qp->next) {
		if(qp->iph->id == id		&&
		   qp->iph->saddr == saddr	&&
		   qp->iph->daddr == daddr	&&
		   qp->iph->protocol == protocol) {
			del_timer(&qp->timer);
			break;
		}
	}
	return qp;
}

/* Remove an entry from the "incomplete datagrams" queue, either
 * because we completed, reassembled and processed it, or because
 * it timed out.
 *
 * This is called _only_ from BH contexts, on packet reception
 * processing and from frag queue expiration timers.  -DaveM
 */
static void ip_free(struct ipq *qp)
{
	struct ipfrag *fp;

	/* Stop the timer for this entry. */
	del_timer(&qp->timer);

	/* Remove this entry from the "incomplete datagrams" queue. */
	if(qp->next)
		qp->next->pprev = qp->pprev;
	*qp->pprev = qp->next;

	/* Release all fragment data. */
	fp = qp->fragments;
	while (fp) {
		struct ipfrag *xp = fp->next;

		frag_kfree_skb(fp->skb);
		frag_kfree_s(fp, sizeof(struct ipfrag));
		fp = xp;
	}

	/* Release the IP header. */
	frag_kfree_s(qp->iph, 64 + 8);

	/* Finally, release the queue descriptor itself. */
	frag_kfree_s(qp, sizeof(struct ipq));
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(unsigned long arg)
{
	struct ipq *qp = (struct ipq *) arg;

  	if(!qp->fragments)
        {	
#ifdef IP_EXPIRE_DEBUG
	  	printk("warning: possible ip-expire attack\n");
#endif
		goto out;
  	}
  
	/* Send an ICMP "Fragment Reassembly Timeout" message. */
	ip_statistics.IpReasmTimeout++;
	ip_statistics.IpReasmFails++;   
	icmp_send(qp->fragments->skb, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);

out:
	/* Nuke the fragment queue. */
	ip_free(qp);
}

/* Memory limiting on fragments.  Evictor trashes the oldest 
 * fragment queue until we are back under the low threshold.
 */
static void ip_evictor(void)
{
	int i, progress;

restart:
	progress = 0;
	/* FIXME: Make LRU queue of frag heads. -DaveM */
	for (i = 0; i < IPQ_HASHSZ; i++) {
		struct ipq *qp;
		if (atomic_read(&ip_frag_mem) <= sysctl_ipfrag_low_thresh)
			return;
		/* We are in a BH context, so these queue
		 * accesses are safe.  -DaveM
		 */
		qp = ipq_hash[i];
		if (qp) {
			/* find the oldest queue for this hash bucket */
			while (qp->next)
				qp = qp->next;
			ip_free(qp);
			progress = 1;
		}
	}
	if (progress)
		goto restart;
	panic("ip_evictor: memcount");
}

/* Add an entry to the 'ipq' queue for a newly received IP datagram.
 * We will (hopefully :-) receive all other fragments of this datagram
 * in time, so we just create a queue for this datagram, in which we
 * will insert the received fragments at their respective positions.
 */
static struct ipq *ip_create(struct sk_buff *skb, struct iphdr *iph)
{
	struct ipq *qp;
	unsigned int hash;
	int ihlen;

	qp = (struct ipq *) frag_kmalloc(sizeof(struct ipq), GFP_ATOMIC);
	if (qp == NULL)
		goto out_nomem;

	/* Allocate memory for the IP header (plus 8 octets for ICMP). */
	ihlen = iph->ihl * 4;

	qp->iph = (struct iphdr *) frag_kmalloc(64 + 8, GFP_ATOMIC);
	if (qp->iph == NULL)
		goto out_free;

	memcpy(qp->iph, iph, ihlen + 8);
	qp->len = 0;
	qp->ihlen = ihlen;
	qp->fragments = NULL;
	qp->dev = skb->dev;

	/* Initialize a timer for this entry. */
	init_timer(&qp->timer);
	qp->timer.expires = 0;			/* (to be set later)	*/
	qp->timer.data = (unsigned long) qp;	/* pointer to queue	*/
	qp->timer.function = ip_expire;		/* expire function	*/

	/* Add this entry to the queue. */
	hash = ipqhashfn(iph->id, iph->saddr, iph->daddr, iph->protocol);

	/* We are in a BH context, no locking necessary.  -DaveM */
	if((qp->next = ipq_hash[hash]) != NULL)
		qp->next->pprev = &qp->next;
	ipq_hash[hash] = qp;
	qp->pprev = &ipq_hash[hash];

	return qp;

out_free:
	frag_kfree_s(qp, sizeof(struct ipq));
out_nomem:
	NETDEBUG(printk(KERN_ERR "IP: create: no memory left !\n"));
	return(NULL);
}

/* See if a fragment queue is complete. */
static int ip_done(struct ipq *qp)
{
	struct ipfrag *fp;
	int offset;

	/* Only possible if we received the final fragment. */
	if (qp->len == 0)
		return 0;

	/* Check all fragment offsets to see if they connect. */
	fp = qp->fragments;
	offset = 0;
	while (fp) {
		if (fp->offset > offset)
			return(0);	/* fragment(s) missing */
		offset = fp->end;
		fp = fp->next;
	}

	/* All fragments are present. */
	return 1;
}

/* Build a new IP datagram from all its fragments.
 *
 * FIXME: We copy here because we lack an effective way of handling lists
 * of bits on input. Until the new skb data handling is in I'm not going
 * to touch this with a bargepole. 
 */
static struct sk_buff *ip_glue(struct ipq *qp)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct ipfrag *fp;
	unsigned char *ptr;
	int count, len;

	/* Allocate a new buffer for the datagram. */
	len = qp->ihlen + qp->len;
	
	if(len > 65535)
		goto out_oversize;
	
	skb = dev_alloc_skb(len);
	if (!skb)
		goto out_nomem;

	/* Fill in the basic details. */
	skb->mac.raw = ptr = skb->data;
	skb->nh.iph = iph = (struct iphdr *) skb_put(skb, len);

	/* Copy the original IP headers into the new buffer. */
	memcpy(ptr, qp->iph, qp->ihlen);
	ptr += qp->ihlen;

	/* Copy the data portions of all fragments into the new buffer. */
	fp = qp->fragments;
	count = qp->ihlen;
	while(fp) {
		if ((fp->len <= 0) || ((count + fp->len) > skb->len))
			goto out_invalid;
		memcpy((ptr + fp->offset), fp->ptr, fp->len);
		if (count == qp->ihlen) {
			skb->dst = dst_clone(fp->skb->dst);
			skb->dev = fp->skb->dev;
		}
		count += fp->len;
		fp = fp->next;
	}

	skb->pkt_type = qp->fragments->skb->pkt_type;
	skb->protocol = qp->fragments->skb->protocol;
	/*
	*  Clearly bogus, because security markings of the individual
	*  fragments should have been checked for consistency before
	*  gluing, and intermediate coalescing of fragments may have
	*  taken place in ip_defrag() before ip_glue() ever got called.
	*  If we're not going to do the consistency checking, we might
	*  as well take the value associated with the first fragment.
	*	--rct
	*/
	skb->security = qp->fragments->skb->security;

	/* Done with all fragments. Fixup the new IP header. */
	iph = skb->nh.iph;
	iph->frag_off = 0;
	iph->tot_len = htons(count);
	ip_statistics.IpReasmOKs++;
	return skb;

out_invalid:
	NETDEBUG(printk(KERN_ERR
			"Invalid fragment list: Fragment over size.\n"));
	kfree_skb(skb);
	goto out_fail;
out_nomem:
 	NETDEBUG(printk(KERN_ERR 
			"IP: queue_glue: no memory for gluing queue %p\n",
			qp));
	goto out_fail;
out_oversize:
	if (net_ratelimit())
		printk(KERN_INFO
			"Oversized IP packet from %d.%d.%d.%d.\n",
			NIPQUAD(qp->iph->saddr));
out_fail:
	ip_statistics.IpReasmFails++;
	return NULL;
}

/* Process an incoming IP datagram fragment. */
struct sk_buff *ip_defrag(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct ipfrag *prev, *next, *tmp, *tfp;
	struct ipq *qp;
	unsigned char *ptr;
	int flags, offset;
	int i, ihl, end;
	
	ip_statistics.IpReasmReqds++;

	/* Start by cleaning up the memory. */
	if (atomic_read(&ip_frag_mem) > sysctl_ipfrag_high_thresh)
		ip_evictor();

	/*
	 * Look for the entry for this IP datagram in the
	 * "incomplete datagrams" queue. If found, the
	 * timer is removed.
	 */
	qp = ip_find(iph, skb->dst);

	/* Is this a non-fragmented datagram? */
	offset = ntohs(iph->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;

	offset <<= 3;		/* offset is in 8-byte chunks */
	ihl = iph->ihl * 4;

	/*
	 * Check whether to create a fresh queue entry. If the
	 * queue already exists, its timer will be restarted as
	 * long as we continue to receive fragments.
	 */
	if (qp) {
		/* ANK. If the first fragment is received,
		 * we should remember the correct IP header (with options)
		 */
	        if (offset == 0) {
			/* Fragmented frame replaced by unfragmented copy? */
			if ((flags & IP_MF) == 0)
				goto out_freequeue;
			qp->ihlen = ihl;
			memcpy(qp->iph, iph, (ihl + 8));
		}
	} else {
		/* Fragmented frame replaced by unfragmented copy? */
		if ((offset == 0) && ((flags & IP_MF) == 0))
			goto out_skb;

		/* If we failed to create it, then discard the frame. */
		qp = ip_create(skb, iph);
		if (!qp)
			goto out_freeskb;
	}
	
	/* Attempt to construct an oversize packet. */
	if((ntohs(iph->tot_len) + ((int) offset)) > 65535)
		goto out_oversize;

	/* Determine the position of this fragment. */
	end = offset + ntohs(iph->tot_len) - ihl;

	/* Is this the final fragment? */
	if ((flags & IP_MF) == 0)
		qp->len = end;

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for(next = qp->fragments; next != NULL; next = next->next) {
		if (next->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* Point into the IP datagram 'data' part. */
	ptr = skb->data + ihl;

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if ((prev != NULL) && (offset < prev->end)) {
		i = prev->end - offset;
		offset += i;	/* ptr into datagram */
		ptr += i;	/* ptr into fragment data */
	}

	/* Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */
	for (tmp = next; tmp != NULL; tmp = tfp) {
		tfp = tmp->next;
		if (tmp->offset >= end)
			break;		/* no overlaps at all	*/

		i = end - next->offset;	/* overlap is 'i' bytes */
		tmp->len -= i;		/* so reduce size of	*/
		tmp->offset += i;	/* next fragment	*/
		tmp->ptr += i;

		/* If we get a frag size of <= 0, remove it and the packet
		 * that it goes with.
		 */
		if (tmp->len <= 0) {
			if (tmp->prev != NULL)
				tmp->prev->next = tmp->next;
			else
				qp->fragments = tmp->next;

			if (tmp->next != NULL)
				tmp->next->prev = tmp->prev;
			
			/* We have killed the original next frame. */
			next = tfp;

			frag_kfree_skb(tmp->skb);
			frag_kfree_s(tmp, sizeof(struct ipfrag));
		}
	}

	/*
	 * Create a fragment to hold this skb.
	 * No memory to save the fragment? throw the lot ...
	 */
	tfp = ip_frag_create(offset, end, skb, ptr);
	if (!tfp)
		goto out_freeskb;

	/* Insert this fragment in the chain of fragments. */
	tfp->prev = prev;
	tfp->next = next;
	if (prev != NULL)
		prev->next = tfp;
	else
		qp->fragments = tfp;

	if (next != NULL)
		next->prev = tfp;

	/* OK, so we inserted this new fragment into the chain.
	 * Check if we now have a full IP datagram which we can
	 * bump up to the IP layer...
	 */
	if (ip_done(qp)) {
		/* Glue together the fragments. */
 		skb = ip_glue(qp);
		/* Free the queue entry. */
out_freequeue:
		ip_free(qp);
out_skb:
		return skb;
	}

	/*
	 * The queue is still active ... reset its timer.
	 */
out_timer:
	mod_timer(&qp->timer, jiffies + sysctl_ipfrag_time); /* ~ 30 seconds */
out:
	return NULL;

	/*
	 * Error exits ... we need to reset the timer if there's a queue.
	 */
out_oversize:
	if (net_ratelimit())
		printk(KERN_INFO "Oversized packet received from %d.%d.%d.%d\n",
			NIPQUAD(iph->saddr));
	/* the skb isn't in a fragment, so fall through to free it */
out_freeskb:
	kfree_skb(skb);
	ip_statistics.IpReasmFails++;
	if (qp)
		goto out_timer;
	goto out;
}
