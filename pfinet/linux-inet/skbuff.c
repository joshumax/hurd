/*
 *	Routines having to do with the 'struct sk_buff' memory handlers.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *			Florian La Roche <rzsfl@rz.uni-sb.de>
 *
 *	Fixes:	
 *		Alan Cox	:	Fixed the worst of the load balancer bugs.
 *		Dave Platt	:	Interrupt stacking fix
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

/*
 *	Note: There are a load of cli()/sti() pairs protecting the net_memory type
 *	variables. Without them for some reason the ++/-- operators do not come out
 *	atomic. Also with gcc 2.4.5 these counts can come out wrong anyway - use 2.5.8!!
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include <linux/string.h>
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include <linux/skbuff.h>
#include "sock.h"


/*
 *	Resource tracking variables
 */

volatile unsigned long net_memory = 0;
volatile unsigned long net_skbcount = 0;
volatile unsigned long net_locked = 0;
volatile unsigned long net_allocs = 0;
volatile unsigned long net_fails  = 0;
volatile unsigned long net_free_locked = 0;

void show_net_buffers(void)
{
	printk("Networking buffers in use          : %lu\n",net_skbcount);
	printk("Memory committed to network buffers: %lu\n",net_memory);
	printk("Network buffers locked by drivers  : %lu\n",net_locked);
	printk("Total network buffer allocations   : %lu\n",net_allocs);
	printk("Total failed network buffer allocs : %lu\n",net_fails);
	printk("Total free while locked events     : %lu\n",net_free_locked);
}

#if CONFIG_SKB_CHECK

/*
 *	Debugging paranoia. Can go later when this crud stack works
 */

int skb_check(struct sk_buff *skb, int head, int line, char *file)
{
	if (head) {
		if (skb->magic_debug_cookie != SK_HEAD_SKB) {
			printk("File: %s Line %d, found a bad skb-head\n",
				file,line);
			return -1;
		}
		if (!skb->next || !skb->prev) {
			printk("skb_check: head without next or prev\n");
			return -1;
		}
		if (skb->next->magic_debug_cookie != SK_HEAD_SKB
			&& skb->next->magic_debug_cookie != SK_GOOD_SKB) {
			printk("File: %s Line %d, bad next head-skb member\n",
				file,line);
			return -1;
		}
		if (skb->prev->magic_debug_cookie != SK_HEAD_SKB
			&& skb->prev->magic_debug_cookie != SK_GOOD_SKB) {
			printk("File: %s Line %d, bad prev head-skb member\n",
				file,line);
			return -1;
		}
#if 0
		{
		struct sk_buff *skb2 = skb->next;
		int i = 0;
		while (skb2 != skb && i < 5) {
			if (skb_check(skb2, 0, line, file) < 0) {
				printk("bad queue element in whole queue\n");
				return -1;
			}
			i++;
			skb2 = skb2->next;
		}
		}
#endif
		return 0;
	}
	if (skb->next != NULL && skb->next->magic_debug_cookie != SK_HEAD_SKB
		&& skb->next->magic_debug_cookie != SK_GOOD_SKB) {
		printk("File: %s Line %d, bad next skb member\n",
			file,line);
		return -1;
	}
	if (skb->prev != NULL && skb->prev->magic_debug_cookie != SK_HEAD_SKB
		&& skb->prev->magic_debug_cookie != SK_GOOD_SKB) {
		printk("File: %s Line %d, bad prev skb member\n",
			file,line);
		return -1;
	}


	if(skb->magic_debug_cookie==SK_FREED_SKB)
	{
		printk("File: %s Line %d, found a freed skb lurking in the undergrowth!\n",
			file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->free);
		return -1;
	}
	if(skb->magic_debug_cookie!=SK_GOOD_SKB)
	{
		printk("File: %s Line %d, passed a non skb!\n", file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld, free=%d\n",
			skb,skb->truesize,skb->mem_len,skb->free);
		return -1;
	}
	if(skb->mem_len!=skb->truesize)
	{
		printk("File: %s Line %d, Dubious size setting!\n",file,line);
		printk("skb=%p, real size=%ld, claimed size=%ld\n",
			skb,skb->truesize,skb->mem_len);
		return -1;
	}
	/* Guess it might be acceptable then */
	return 0;
}
#endif


#ifdef CONFIG_SKB_CHECK
void skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
	list->magic_debug_cookie = SK_HEAD_SKB;
}


/*
 *	Insert an sk_buff at the start of a list.
 */
void skb_queue_head(struct sk_buff_head *list_,struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	IS_SKB(newsk);
	IS_SKB_HEAD(list);
	if (newsk->next || newsk->prev)
		printk("Suspicious queue head: sk_buff on list!\n");

	newsk->next = list->next;
	newsk->prev = list;

	newsk->next->prev = newsk;
	newsk->prev->next = newsk;

	restore_flags(flags);
}

/*
 *	Insert an sk_buff at the end of a list.
 */
void skb_queue_tail(struct sk_buff_head *list_, struct sk_buff *newsk)
{
	unsigned long flags;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	if (newsk->next || newsk->prev)
		printk("Suspicious queue tail: sk_buff on list!\n");
	IS_SKB(newsk);
	IS_SKB_HEAD(list);

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

struct sk_buff *skb_dequeue(struct sk_buff_head *list_)
{
	long flags;
	struct sk_buff *result;
	struct sk_buff *list = (struct sk_buff *)list_;

	save_flags(flags);
	cli();

	IS_SKB_HEAD(list);

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

	IS_SKB(result);
	return result;
}

/*
 *	Insert a packet before another one in a list.
 */
void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("insert before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("inserted item is already on a list.\n");

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
void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	IS_SKB(old);
	IS_SKB(newsk);

	if(!old->next || !old->prev)
		printk("append before unlisted item!\n");
	if(newsk->next || newsk->prev)
		printk("append item is already on a list.\n");

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
void skb_unlink(struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	IS_SKB(skb);

	if(skb->prev && skb->next)
	{
		skb->next->prev = skb->prev;
		skb->prev->next = skb->next;
		skb->next = NULL;
		skb->prev = NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE	/* This is legal but we sometimes want to watch it */
	else
		printk("skb_unlink: not a linked element\n");
#endif
	restore_flags(flags);
}

#endif

/*
 *	Free an sk_buff. This still knows about things it should
 *	not need to like protocols and sockets.
 */

void kfree_skb(struct sk_buff *skb, int rw)
{
	if (skb == NULL)
	{
		printk("kfree_skb: skb = NULL (from %p)\n",
			__builtin_return_address(0));
		return;
  	}
#ifdef CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif
	if (skb->lock)
	{
		skb->free = 3;    /* Free when unlocked */
		net_free_locked++;
		return;
  	}
  	if (skb->free == 2)
		printk("Warning: kfree_skb passed an skb that nobody set the free flag on! (from %p)\n",
			__builtin_return_address(0));
	if (skb->next)
	 	printk("Warning: kfree_skb passed an skb still on a list (from %p).\n",
			__builtin_return_address(0));
	if (skb->sk)
	{
	        if(skb->sk->prot!=NULL)
		{
			if (rw)
		     		skb->sk->prot->rfree(skb->sk, skb, skb->mem_len);
		     	else
		     		skb->sk->prot->wfree(skb->sk, skb, skb->mem_len);

		}
		else
		{
			unsigned long flags;
			/* Non INET - default wmalloc/rmalloc handler */
			save_flags(flags);
			cli();
			if (rw)
				skb->sk->rmem_alloc-=skb->mem_len;
			else
				skb->sk->wmem_alloc-=skb->mem_len;
			restore_flags(flags);
			if(!skb->sk->dead)
				skb->sk->write_space(skb->sk);
			kfree_skbmem(skb,skb->mem_len);
		}
	}
	else
		kfree_skbmem(skb, skb->mem_len);
}

/*
 *	Allocate a new skbuff. We do this ourselves so we can fill in a few 'private'
 *	fields and also do memory statistics to find all the [BEEP] leaks.
 */
struct sk_buff *alloc_skb(unsigned int size,int priority)
{
	struct sk_buff *skb;
	unsigned long flags;

	if (intr_count && priority!=GFP_ATOMIC) {
		static int count = 0;
		if (++count < 5) {
			printk("alloc_skb called nonatomically from interrupt %p\n",
				__builtin_return_address(0));
			priority = GFP_ATOMIC;
		}
	}

	size+=sizeof(struct sk_buff);
	skb=(struct sk_buff *)kmalloc(size,priority);
	if (skb == NULL)
	{
		net_fails++;
		return NULL;
	}
#ifdef PARANOID_BUGHUNT_MODE
	if(skb->magic_debug_cookie == SK_GOOD_SKB)
		printk("Kernel kmalloc handed us an existing skb (%p)\n",skb);
#endif

	net_allocs++;

	skb->free = 2;	/* Invalid so we pick up forgetful users */
	skb->lock = 0;
	skb->pkt_type = PACKET_HOST;	/* Default type */
	skb->truesize = size;
	skb->mem_len = size;
	skb->mem_addr = skb;
#ifdef CONFIG_SLAVE_BALANCING
	skb->in_dev_queue = 0;
#endif
	skb->fraglist = NULL;
	skb->prev = skb->next = NULL;
	skb->link3 = NULL;
	skb->sk = NULL;
	skb->localroute=0;
	skb->stamp.tv_sec=0;	/* No idea about time */
	skb->localroute = 0;
	save_flags(flags);
	cli();
	net_memory += size;
	net_skbcount++;
	restore_flags(flags);
#if CONFIG_SKB_CHECK
	skb->magic_debug_cookie = SK_GOOD_SKB;
#endif
	skb->users = 0;
	return skb;
}

/*
 *	Free an skbuff by memory
 */

void kfree_skbmem(struct sk_buff *skb,unsigned size)
{
	unsigned long flags;
#ifdef CONFIG_SLAVE_BALANCING
	save_flags(flags);
	cli();
	if(skb->in_dev_queue && skb->dev!=NULL)
		skb->dev->pkt_queue--;
	restore_flags(flags);
#endif
#ifdef CONFIG_SKB_CHECK
	IS_SKB(skb);
	if(size!=skb->truesize)
		printk("kfree_skbmem: size mismatch.\n");

	if(skb->magic_debug_cookie == SK_GOOD_SKB)
	{
		save_flags(flags);
		cli();
		IS_SKB(skb);
		skb->magic_debug_cookie = SK_FREED_SKB;
		kfree_s((void *)skb,size);
		net_skbcount--;
		net_memory -= size;
		restore_flags(flags);
	}
	else
		printk("kfree_skbmem: bad magic cookie\n");
#else
	save_flags(flags);
	cli();
	kfree_s((void *)skb,size);
	net_skbcount--;
	net_memory -= size;
	restore_flags(flags);
#endif
}

/*
 *	Duplicate an sk_buff. The new one is not owned by a socket or locked
 *	and will be freed on deletion.
 */

struct sk_buff *skb_clone(struct sk_buff *skb, int priority)
{
	struct sk_buff *n;
	unsigned long offset;

	n=alloc_skb(skb->mem_len-sizeof(struct sk_buff),priority);
	if(n==NULL)
		return NULL;

	offset=((char *)n)-((char *)skb);

	memcpy(n->data,skb->data,skb->mem_len-sizeof(struct sk_buff));
	n->len=skb->len;
	n->link3=NULL;
	n->sk=NULL;
	n->when=skb->when;
	n->dev=skb->dev;
	n->h.raw=skb->h.raw+offset;
	n->ip_hdr=(struct iphdr *)(((char *)skb->ip_hdr)+offset);
	n->fraglen=skb->fraglen;
	n->fraglist=skb->fraglist;
	n->saddr=skb->saddr;
	n->daddr=skb->daddr;
	n->raddr=skb->raddr;
	n->acked=skb->acked;
	n->used=skb->used;
	n->free=1;
	n->arp=skb->arp;
	n->tries=0;
	n->lock=0;
	n->users=0;
	n->pkt_type=skb->pkt_type;
	return n;
}


/*
 *     Skbuff device locking
 */

void skb_device_lock(struct sk_buff *skb)
{
	if(skb->lock)
		printk("double lock on device queue!\n");
	else
		net_locked++;
	skb->lock++;
}

void skb_device_unlock(struct sk_buff *skb)
{
	if(skb->lock==0)
		printk("double unlock on device queue!\n");
	skb->lock--;
	if(skb->lock==0)
		net_locked--;
}

void dev_kfree_skb(struct sk_buff *skb, int mode)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if(skb->lock==1)
		net_locked--;

	if (!--skb->lock && (skb->free == 1 || skb->free == 3))
	{
		restore_flags(flags);
		kfree_skb(skb,mode);
	}
	else
		restore_flags(flags);
}

int skb_device_locked(struct sk_buff *skb)
{
	return skb->lock? 1 : 0;
}

