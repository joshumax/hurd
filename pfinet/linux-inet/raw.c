/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	@(#)raw.c	1.0.4	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared skbuff
 *					library. No more peek crashes, no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "icmp.h"
#include "udp.h"


static inline unsigned long min(unsigned long a, unsigned long b)
{
	if (a < b) 
		return(a);
	return(b);
}


/* raw_err gets called by the icmp module. */
void raw_err (int err, unsigned char *header, unsigned long daddr,
	 unsigned long saddr, struct inet_protocol *protocol)
{
	struct sock *sk;
   
	if (protocol == NULL) 
		return;
	sk = (struct sock *) protocol->data;
	if (sk == NULL) 
		return;

	/* This is meaningless in raw sockets. */
	if (err & 0xff00 == (ICMP_SOURCE_QUENCH << 8)) 
	{
		if (sk->cong_window > 1) sk->cong_window = sk->cong_window/2;
		return;
	}

	sk->err = icmp_err_convert[err & 0xff].errno;
	sk->error_report(sk);
  
	return;
}


/*
 *	This should be the easiest of all, all we do is
 *	copy it into a buffer. All demultiplexing is done
 *	in ip.c
 */

int raw_rcv(struct sock *sk, struct sk_buff *skb, struct device *dev, long saddr, long daddr)
{
	/* Now we need to copy this into memory. */
	skb->sk = sk;
	skb->len = ntohs(skb->ip_hdr->tot_len);
	skb->h.raw = (unsigned char *) skb->ip_hdr;
	skb->dev = dev;
	skb->saddr = daddr;
	skb->daddr = saddr;

	/* Charge it to the socket. */
	
	if(sock_queue_rcv_skb(sk,skb)<0)
	{
		ip_statistics.IpInDiscards++;
		skb->sk=NULL;
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	ip_statistics.IpInDelivers++;
	release_sock(sk);
	return(0);
}

/*
 *	Send a RAW IP packet.
 */

static int raw_sendto(struct sock *sk, unsigned char *from, 
	int len, int noblock, unsigned flags, struct sockaddr_in *usin, int addr_len)
{
	struct sk_buff *skb;
	struct device *dev=NULL;
	struct sockaddr_in sin;
	int tmp;
	int err;

	/*
	 *	Check the flags. Only MSG_DONTROUTE is permitted.
	 */

	if (flags & MSG_OOB)		/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;
			 
	if (flags & ~MSG_DONTROUTE)
		return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */

	if (usin) 
	{
		if (addr_len < sizeof(sin)) 
			return(-EINVAL);
		memcpy(&sin, usin, sizeof(sin));
		if (sin.sin_family && sin.sin_family != AF_INET) 
			return(-EINVAL);
	}
	else 
	{
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		sin.sin_family = AF_INET;
		sin.sin_port = sk->protocol;
		sin.sin_addr.s_addr = sk->daddr;
	}
	if (sin.sin_port == 0) 
		sin.sin_port = sk->protocol;
  
	if (sin.sin_addr.s_addr == INADDR_ANY)
		sin.sin_addr.s_addr = ip_my_addr();

	if (sk->broadcast == 0 && ip_chk_addr(sin.sin_addr.s_addr)==IS_BROADCAST)
		return -EACCES;

	skb=sock_alloc_send_skb(sk, len+sk->prot->max_header, noblock, &err);
	if(skb==NULL)
		return err;
		
	skb->sk = sk;
	skb->free = 1;
	skb->localroute = sk->localroute | (flags&MSG_DONTROUTE);

	tmp = sk->prot->build_header(skb, sk->saddr, 
			       sin.sin_addr.s_addr, &dev,
			       sk->protocol, sk->opt, skb->mem_len, sk->ip_tos,sk->ip_ttl);
	if (tmp < 0) 
	{
		kfree_skb(skb,FREE_WRITE);
		release_sock(sk);
		return(tmp);
	}

	memcpy_fromfs(skb->data + tmp, from, len);

	/*
	 *	If we are using IPPROTO_RAW, we need to fill in the source address in
	 *     	the IP header 
	 */

	if(sk->protocol==IPPROTO_RAW) 
	{
		unsigned char *buff;
		struct iphdr *iph;

		buff = skb->data;
		buff += tmp;

		iph = (struct iphdr *)buff;
		iph->saddr = sk->saddr;
	}

	skb->len = tmp + len;
  
	sk->prot->queue_xmit(sk, dev, skb, 1);
	release_sock(sk);
	return(len);
}


static int raw_write(struct sock *sk, unsigned char *buff, int len, int noblock,
	   unsigned flags)
{
	return(raw_sendto(sk, buff, len, noblock, flags, NULL, 0));
}


static void raw_close(struct sock *sk, int timeout)
{
	sk->state = TCP_CLOSE;
}


static int raw_init(struct sock *sk)
{
	return(0);
}


/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int raw_recvfrom(struct sock *sk, unsigned char *to, int len,
     int noblock, unsigned flags, struct sockaddr_in *sin,
	     int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	int err;
	int truesize;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;
		
	if (sk->shutdown & RCV_SHUTDOWN) 
		return(0);

	if (addr_len) 
		*addr_len=sizeof(*sin);

	skb=skb_recv_datagram(sk,flags,noblock,&err);
	if(skb==NULL)
 		return err;

	truesize=skb->len;
	copied = min(len, truesize);
  
	skb_copy_datagram(skb, 0, to, copied);
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) 
	{
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = skb->daddr;
	}
	skb_free_datagram(skb);
	release_sock(sk);
	return (truesize);	/* len not copied. BSD returns the true size of the message so you know a bit fell off! */
}


int raw_read (struct sock *sk, unsigned char *buff, int len, int noblock,unsigned flags)
{
	return(raw_recvfrom(sk, buff, len, noblock, flags, NULL, NULL));
}


struct proto raw_prot = {
	sock_wmalloc,
	sock_rmalloc,
	sock_wfree,
	sock_rfree,
	sock_rspace,
	sock_wspace,
	raw_close,
	raw_read,
	raw_write,
	raw_sendto,
	raw_recvfrom,
	ip_build_header,
	udp_connect,
	NULL,
	ip_queue_xmit,
	NULL,
	NULL,
	NULL,
	NULL,
	datagram_select,
	NULL,
	raw_init,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	128,
	0,
	{NULL,},
	"RAW",
	0, 0
};
