/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PACKET - implements raw packet sockets.
 *
 * Version:	@(#)packet.c	1.0.6	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:	
 *		Alan Cox	:	verify_area() now used correctly
 *		Alan Cox	:	new skbuff lists, look ma no backlogs!
 *		Alan Cox	:	tidied skbuff lists.
 *		Alan Cox	:	Now uses generic datagram routines I
 *					added. Also fixed the peek/read crash
 *					from all old Linux datagram code.
 *		Alan Cox	:	Uses the improved datagram code.
 *		Alan Cox	:	Added NULL's for socket options.
 *		Alan Cox	:	Re-commented the code.
 *		Alan Cox	:	Use new kernel side addressing
 *		Rob Janssen	:	Correct MTU usage.
 *		Dave Platt	:	Counter leaks caused by incorrect
 *					interrupt locking and some slightly
 *					dubious gcc output. Can you read
 *					compiler: it said _VOLATILE_
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include <linux/skbuff.h>
#include "sock.h"
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/segment.h>

/*
 *	We really ought to have a single public _inline_ min function!
 */

static unsigned long min(unsigned long a, unsigned long b)
{
	if (a < b) 
		return(a);
	return(b);
}


/*
 *	This should be the easiest of all, all we do is copy it into a buffer. 
 */
 
int packet_rcv(struct sk_buff *skb, struct device *dev,  struct packet_type *pt)
{
	struct sock *sk;
	unsigned long flags;
	
	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = (struct sock *) pt->data;	

	/*
	 *	The SOCK_PACKET socket receives _all_ frames, and as such 
	 *	therefore needs to put the header back onto the buffer.
	 *	(it was removed by inet_bh()).
	 */
	 
	skb->dev = dev;
	skb->len += dev->hard_header_len;

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */
	 
	if (sk->rmem_alloc & 0xFF000000) {
		printk("packet_rcv: sk->rmem_alloc = %ld\n", sk->rmem_alloc);
		sk->rmem_alloc = 0;
	}

	if (sk->rmem_alloc + skb->mem_len >= sk->rcvbuf) 
	{
/*	        printk("packet_rcv: drop, %d+%d>%d\n", sk->rmem_alloc, skb->mem_len, sk->rcvbuf); */
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	save_flags(flags);
	cli();

	skb->sk = sk;
	sk->rmem_alloc += skb->mem_len;	

	/*
	 *	Queue the packet up, and wake anyone waiting for it.
	 */

	skb_queue_tail(&sk->receive_queue,skb);
	if(!sk->dead)
		sk->data_ready(sk,skb->len);
		
	restore_flags(flags);

	/*
	 *	Processing complete.
	 */
	 
	release_sock(sk);	/* This is now effectively surplus in this layer */
	return(0);
}


/*
 *	Output a raw packet to a device layer. This bypasses all the other
 *	protocol layers and you must therefore supply it with a complete frame
 */
 
static int packet_sendto(struct sock *sk, unsigned char *from, int len,
	      int noblock, unsigned flags, struct sockaddr_in *usin,
	      int addr_len)
{
	struct sk_buff *skb;
	struct device *dev;
	struct sockaddr *saddr=(struct sockaddr *)usin;

	/*
	 *	Check the flags. 
	 */

	if (flags) 
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */
	 
	if (usin) 
	{
		if (addr_len < sizeof(*saddr)) 
			return(-EINVAL);
	} 
	else
		return(-EINVAL);	/* SOCK_PACKET must be sent giving an address */
	
	/*
	 *	Find the device first to size check it 
	 */

	saddr->sa_data[13] = 0;
	dev = dev_get(saddr->sa_data);
	if (dev == NULL) 
	{
		return(-ENXIO);
  	}
	
	/*
	 *	You may not queue a frame bigger than the mtu. This is the lowest level
	 *	raw protocol and you must do your own fragmentation at this level.
	 */
	 
	if(len>dev->mtu+dev->hard_header_len)
  		return -EMSGSIZE;

	skb = sk->prot->wmalloc(sk, len, 0, GFP_KERNEL);

	/*
	 *	If the write buffer is full, then tough. At this level the user gets to
	 *	deal with the problem - do your own algorithmic backoffs.
	 */
	 
	if (skb == NULL) 
	{
		return(-ENOBUFS);
	}
	
	/*
	 *	Fill it in 
	 */
	 
	skb->sk = sk;
	skb->free = 1;
	memcpy_fromfs(skb->data, from, len);
	skb->len = len;
	skb->arp = 1;		/* No ARP needs doing on this (complete) frame */

	/*
	 *	Now send it
	 */

	if (dev->flags & IFF_UP) 
		dev_queue_xmit(skb, dev, sk->priority);
	else
		kfree_skb(skb, FREE_WRITE);
	return(len);
}

/*
 *	A write to a SOCK_PACKET can't actually do anything useful and will
 *	always fail but we include it for completeness and future expansion.
 */

static int packet_write(struct sock *sk, unsigned char *buff, 
	     int len, int noblock,  unsigned flags)
{
	return(packet_sendto(sk, buff, len, noblock, flags, NULL, 0));
}

/*
 *	Close a SOCK_PACKET socket. This is fairly simple. We immediately go
 *	to 'closed' state and remove our protocol entry in the device list.
 *	The release_sock() will destroy the socket if a user has closed the
 *	file side of the object.
 */

static void packet_close(struct sock *sk, int timeout)
{
	sk->inuse = 1;
	sk->state = TCP_CLOSE;
	dev_remove_pack((struct packet_type *)sk->pair);
	kfree_s((void *)sk->pair, sizeof(struct packet_type));
	sk->pair = NULL;
	release_sock(sk);
}

/*
 *	Create a packet of type SOCK_PACKET. We do one slightly irregular
 *	thing here that wants tidying up. We borrow the 'pair' pointer in
 *	the socket object so we can find the packet_type entry in the
 *	device list. The reverse is easy as we use the data field of the
 *	packet type to point to our socket.
 */

static int packet_init(struct sock *sk)
{
	struct packet_type *p;

	p = (struct packet_type *) kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) 
		return(-ENOMEM);

	p->func = packet_rcv;
	p->type = sk->num;
	p->data = (void *)sk;
	p->dev = NULL;
	dev_add_pack(p);
   
	/*
	 *	We need to remember this somewhere. 
	 */
   
	sk->pair = (struct sock *)p;

	return(0);
}


/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */
 
int packet_recvfrom(struct sock *sk, unsigned char *to, int len,
	        int noblock, unsigned flags, struct sockaddr_in *sin,
	        int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	struct sockaddr *saddr;
	int err;
	int truesize;

	saddr = (struct sockaddr *)sin;

	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);
		
	/*
	 *	If the address length field is there to be filled in, we fill
	 *	it in now.
	 */

	if (addr_len) 
		*addr_len=sizeof(*saddr);
	
	/*
	 *	Call the generic datagram receiver. This handles all sorts
	 *	of horrible races and re-entrancy so we can forget about it
	 *	in the protocol layers.
	 */
	 
	skb=skb_recv_datagram(sk,flags,noblock,&err);
	
	/*
	 *	An error occurred so return it. Because skb_recv_datagram() 
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */
	 
	if(skb==NULL)
		return err;
		
	/*
	 *	You lose any data beyond the buffer you gave. If it worries a
	 *	user program they can ask the device for its MTU anyway.
	 */
	 
	truesize = skb->len;
	copied = min(len, truesize);

	memcpy_tofs(to, skb->data, copied);	/* We can't use skb_copy_datagram here */

	/*
	 *	Copy the address. 
	 */
	 
	if (saddr) 
	{
		saddr->sa_family = skb->dev->type;
		memcpy(saddr->sa_data,skb->dev->name, 14);
	}
	
	/*
	 *	Free or return the buffer as appropriate. Again this hides all the
	 *	races and re-entrancy issues from us.
	 */

	skb_free_datagram(skb);

	/*
	 *	We are done.
	 */
	 
	release_sock(sk);
	return(truesize);
}


/*
 *	A packet read can succeed and is just the same as a recvfrom but without the
 *	addresses being recorded.
 */

int packet_read(struct sock *sk, unsigned char *buff,
	    int len, int noblock, unsigned flags)
{
	return(packet_recvfrom(sk, buff, len, noblock, flags, NULL, NULL));
}


/*
 *	This structure declares to the lower layer socket subsystem currently
 *	incorrectly embedded in the IP code how to behave. This interface needs
 *	a lot of work and will change.
 */
 
struct proto packet_prot = 
{
	sock_wmalloc,
	sock_rmalloc,
	sock_wfree,
	sock_rfree,
	sock_rspace,
	sock_wspace,
	packet_close,
	packet_read,
	packet_write,
	packet_sendto,
	packet_recvfrom,
	ip_build_header,	/* Not actually used */
	NULL,
	NULL,
	ip_queue_xmit,		/* These two are not actually used */
	NULL,
	NULL,
	NULL,
	NULL, 
	datagram_select,
	NULL,
	packet_init,
	NULL,
	NULL,			/* No set/get socket options */
	NULL,
	128,
	0,
	{NULL,},
	"PACKET",
	0, 0
};
