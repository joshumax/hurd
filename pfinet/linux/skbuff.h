/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H
#include <linux/malloc.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/config.h>

#undef CONFIG_SKB_CHECK

#define HAVE_ALLOC_SKB		/* For the drivers to know */


#define FREE_READ	1
#define FREE_WRITE	0


struct sk_buff_head {
  struct sk_buff		* volatile next;
  struct sk_buff		* volatile prev;
#if CONFIG_SKB_CHECK
  int				magic_debug_cookie;
#endif
};


struct sk_buff {
  struct sk_buff		* volatile next;
  struct sk_buff		* volatile prev;
#if CONFIG_SKB_CHECK
  int				magic_debug_cookie;
#endif
  struct sk_buff		* volatile link3;
  struct sock			*sk;
  volatile unsigned long	when;	/* used to compute rtt's	*/
  struct timeval		stamp;
  struct device			*dev;
  struct sk_buff		*mem_addr;
  union {
	struct tcphdr	*th;
	struct ethhdr	*eth;
	struct iphdr	*iph;
	struct udphdr	*uh;
	unsigned char	*raw;
	unsigned long	seq;
  } h;
  struct iphdr		*ip_hdr;		/* For IPPROTO_RAW */
  unsigned long			mem_len;
  unsigned long 		len;
  unsigned long			fraglen;
  struct sk_buff		*fraglist;	/* Fragment list */
  unsigned long			truesize;
  unsigned long 		saddr;
  unsigned long 		daddr;
  unsigned long			raddr;		/* next hop addr */
  volatile char 		acked,
				used,
				free,
				arp;
  unsigned char			tries,lock,localroute,pkt_type;
#define PACKET_HOST		0		/* To us */
#define PACKET_BROADCAST	1
#define PACKET_MULTICAST	2
#define PACKET_OTHERHOST	3		/* Unmatched promiscuous */
  unsigned short		users;		/* User count - see datagram.c (and soon seqpacket.c/stream.c) */
  unsigned short		pkt_class;	/* For drivers that need to cache the packet type with the skbuff (new PPP) */
#ifdef CONFIG_SLAVE_BALANCING
  unsigned short		in_dev_queue;
#endif  
  unsigned long			padding[0];
  unsigned char			data[0];
};

#define SK_WMEM_MAX	32767
#define SK_RMEM_MAX	32767

#ifdef CONFIG_SKB_CHECK
#define SK_FREED_SKB	0x0DE2C0DE
#define SK_GOOD_SKB	0xDEC0DED1
#define SK_HEAD_SKB	0x12231298
#endif

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */

#include <asm/system.h>

#if 0
extern void			print_skb(struct sk_buff *);
#endif
extern void			kfree_skb(struct sk_buff *skb, int rw);
extern void			skb_queue_head_init(struct sk_buff_head *list);
extern void			skb_queue_head(struct sk_buff_head *list,struct sk_buff *buf);
extern void			skb_queue_tail(struct sk_buff_head *list,struct sk_buff *buf);
extern struct sk_buff *		skb_dequeue(struct sk_buff_head *list);
extern void 			skb_insert(struct sk_buff *old,struct sk_buff *newsk);
extern void			skb_append(struct sk_buff *old,struct sk_buff *newsk);
extern void			skb_unlink(struct sk_buff *buf);
extern struct sk_buff *		skb_peek_copy(struct sk_buff_head *list);
extern struct sk_buff *		alloc_skb(unsigned int size, int priority);
extern void			kfree_skbmem(struct sk_buff *skb, unsigned size);
extern struct sk_buff *		skb_clone(struct sk_buff *skb, int priority);
extern void			skb_device_lock(struct sk_buff *skb);
extern void			skb_device_unlock(struct sk_buff *skb);
extern void			dev_kfree_skb(struct sk_buff *skb, int mode);
extern int			skb_device_locked(struct sk_buff *skb);
/*
 *	Peek an sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. For an interrupt
 *	type system cli() peek the buffer copy the data and sti();
 */
static __inline__ struct sk_buff *skb_peek(struct sk_buff_head *list_)
{
	struct sk_buff *list = (struct sk_buff *)list_;
	return (list->next != list)? list->next : NULL;
}

#if CONFIG_SKB_CHECK
extern int 			skb_check(struct sk_buff *skb,int,int, char *);
#define IS_SKB(skb)		skb_check((skb), 0, __LINE__,__FILE__)
#define IS_SKB_HEAD(skb)	skb_check((skb), 1, __LINE__,__FILE__)
#else
#define IS_SKB(skb)		
#define IS_SKB_HEAD(skb)	

extern __inline__ void skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
}

/*
 *	Insert an sk_buff at the start of a list.
 */

extern __inline__ void skb_queue_head(struct sk_buff_head *list_,struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();
	newsk->next = list->next;
	newsk->prev = list;
	newsk->next->prev = newsk;
	newsk->prev->next = newsk;
	restore_flags(flags);
}

/*
 *	Insert an sk_buff at the end of a list.
 */

extern __inline__ void skb_queue_tail(struct sk_buff_head *list_, struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	newsk->next = list;
	newsk->prev = list->prev;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Remove an sk_buff from a list. This routine is also interrupt safe
 *	so you can grab read and free buffers as another process adds them.
 */

extern __inline__ struct sk_buff *skb_dequeue(struct sk_buff_head *list_)
{
	long flags;
	struct sk_buff *result;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	result = list->next;
	if (result == list) {
		restore_flags(flags);
		return NULL;
	}

	result->next->prev = list;
	list->next = result->next;

	result->next = NULL;
	result->prev = NULL;

	restore_flags(flags);

	return result;
}

/*
 *	Insert a packet before another one in a list.
 */

extern __inline__ void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	newsk->next = old;
	newsk->prev = old->prev;
	old->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Place a packet after a given packet in a list.
 */

extern __inline__ void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	newsk->prev = old;
	newsk->next = old->next;
	newsk->next->prev = newsk;
	old->next = newsk;

	restore_flags(flags);
}

/*
 *	Remove an sk_buff from its list. Works even without knowing the list it
 *	is sitting on, which can be handy at times. It also means that THE LIST
 *	MUST EXIST when you unlink. Thus a list must have its contents unlinked
 *	_FIRST_.
 */

extern __inline__ void skb_unlink(struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	if(skb->prev && skb->next)
	{
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
	}
	restore_flags(flags);
}

#endif

extern struct sk_buff *		skb_recv_datagram(struct sock *sk,unsigned flags,int noblock, int *err);
extern int			datagram_select(struct sock *sk, int sel_type, select_table *wait);
extern void			skb_copy_datagram(struct sk_buff *from, int offset, char *to,int size);
extern void			skb_free_datagram(struct sk_buff *skb);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
