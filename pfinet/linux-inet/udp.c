/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The User Datagram Protocol (UDP).
 *
 * Version:	@(#)udp.c	1.0.13	06/02/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() calls
 *		Alan Cox	: 	stopped close while in use off icmp
 *					messages. Not a fix but a botch that
 *					for udp at least is 'valid'.
 *		Alan Cox	:	Fixed icmp handling properly
 *		Alan Cox	: 	Correct error for oversized datagrams
 *		Alan Cox	:	Tidied select() semantics. 
 *		Alan Cox	:	udp_err() fixed properly, also now 
 *					select and read wake correctly on errors
 *		Alan Cox	:	udp_send verify_area moved to avoid mem leak
 *		Alan Cox	:	UDP can count its memory
 *		Alan Cox	:	send to an unknown connection causes
 *					an ECONNREFUSED off the icmp, but
 *					does NOT close.
 *		Alan Cox	:	Switched to new sk_buff handlers. No more backlog!
 *		Alan Cox	:	Using generic datagram code. Even smaller and the PEEK
 *					bug no longer crashes it.
 *		Fred Van Kempen	: 	Net2e support for sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram
 *		Alan Cox	:	Added get/set sockopt support.
 *		Alan Cox	:	Broadcasting without option set returns EACCES.
 *		Alan Cox	:	No wakeup calls. Instead we now use the callbacks.
 *		Alan Cox	:	Use ip_tos and ip_ttl
 *		Alan Cox	:	SNMP Mibs
 *		Alan Cox	:	MSG_DONTROUTE, and 0.0.0.0 support.
 *		Matt Dillon	:	UDP length checks.
 *		Alan Cox	:	Smarter af_inet used properly.
 *		Alan Cox	:	Use new kernel side addressing.
 *		Alan Cox	:	Incorrect return on truncated datagram receive.
 *
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
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/termios.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "snmp.h"
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "udp.h"
#include "icmp.h"
#include "route.h"

/*
 *	SNMP MIB for the UDP layer
 */

struct udp_mib		udp_statistics;


static int udp_deliver(struct sock *sk, struct udphdr *uh, struct sk_buff *skb, struct device *dev, long saddr, long daddr, int len);

#define min(a,b)	((a)<(b)?(a):(b))


/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  
 * Header points to the ip header of the error packet. We move
 * on past this. Then (as it used to claim before adjustment)
 * header points to the first 8 bytes of the udp header.  We need
 * to find the appropriate port.
 */

void udp_err(int err, unsigned char *header, unsigned long daddr,
	unsigned long saddr, struct inet_protocol *protocol)
{
	struct udphdr *th;
	struct sock *sk;
	struct iphdr *ip=(struct iphdr *)header;
  
	header += 4*ip->ihl;

	/*
	 *	Find the 8 bytes of post IP header ICMP included for us
	 */  
	
	th = (struct udphdr *)header;  
   
	sk = get_sock(&udp_prot, th->source, daddr, th->dest, saddr);

	if (sk == NULL) 
	  	return;	/* No socket for error */
  	
	if (err & 0xff00 ==(ICMP_SOURCE_QUENCH << 8)) 
	{	/* Slow down! */
		if (sk->cong_window > 1) 
			sk->cong_window = sk->cong_window/2;
		return;
	}

	/*
	 *	Various people wanted BSD UDP semantics. Well they've come 
	 *	back out because they slow down response to stuff like dead
	 *	or unreachable name servers and they screw term users something
	 *	chronic. Oh and it violates RFC1122. So basically fix your 
	 *	client code people.
	 */
	 
#ifdef CONFIG_I_AM_A_BROKEN_BSD_WEENIE
	/*
	 *	It's only fatal if we have connected to them. I'm not happy
	 *	with this code. Some BSD comparisons need doing.
	 */
	 
	if (icmp_err_convert[err & 0xff].fatal && sk->state == TCP_ESTABLISHED) 
	{
		sk->err = icmp_err_convert[err & 0xff].errno;
		sk->error_report(sk);
 	}
#else
	if (icmp_err_convert[err & 0xff].fatal)
	{
		sk->err = icmp_err_convert[err & 0xff].errno;
		sk->error_report(sk);
	}
#endif
}


static unsigned short udp_check(struct udphdr *uh, int len, unsigned long saddr, unsigned long daddr)
{
	unsigned long sum;

	__asm__(  "\t addl %%ecx,%%ebx\n"
		  "\t adcl %%edx,%%ebx\n"
		  "\t adcl $0, %%ebx\n"
		  : "=b"(sum)
		  : "0"(daddr), "c"(saddr), "d"((ntohs(len) << 16) + IPPROTO_UDP*256)
		  : "cx","bx","dx" );

	if (len > 3) 
	{
		__asm__("\tclc\n"
			"1:\n"
			"\t lodsl\n"
			"\t adcl %%eax, %%ebx\n"
			"\t loop 1b\n"
			"\t adcl $0, %%ebx\n"
			: "=b"(sum) , "=S"(uh)
			: "0"(sum), "c"(len/4) ,"1"(uh)
			: "ax", "cx", "bx", "si" );
	}

	/*
	 *	Convert from 32 bits to 16 bits. 
	 */

	__asm__("\t movl %%ebx, %%ecx\n"
		"\t shrl $16,%%ecx\n"
	  	"\t addw %%cx, %%bx\n"
	  	"\t adcw $0, %%bx\n"
	  	: "=b"(sum)
	  	: "0"(sum)
	  	: "bx", "cx");
	
	/* 
	 *	Check for an extra word. 
	 */
	 
	if ((len & 2) != 0) 
	{
		__asm__("\t lodsw\n"
			"\t addw %%ax,%%bx\n"
			"\t adcw $0, %%bx\n"
			: "=b"(sum), "=S"(uh)
			: "0"(sum) ,"1"(uh)
			: "si", "ax", "bx");
  	}

  	/*
  	 *	Now check for the extra byte. 
  	 */
  	 
	if ((len & 1) != 0) 
	{
		__asm__("\t lodsb\n"
			"\t movb $0,%%ah\n"
			"\t addw %%ax,%%bx\n"
			"\t adcw $0, %%bx\n"
			: "=b"(sum)
			: "0"(sum) ,"S"(uh)
			: "si", "ax", "bx");
  	}

  	/* 
  	 *	We only want the bottom 16 bits, but we never cleared the top 16. 
  	 */

	return((~sum) & 0xffff);
}

/*
 *	Generate UDP checksums. These may be disabled, eg for fast NFS over ethernet
 *	We default them enabled.. if you turn them off you either know what you are
 *	doing or get burned...
 */

static void udp_send_check(struct udphdr *uh, unsigned long saddr, 
	       unsigned long daddr, int len, struct sock *sk)
{
	uh->check = 0;
	if (sk && sk->no_check) 
	  	return;
	uh->check = udp_check(uh, len, saddr, daddr);
	
	/*
	 *	FFFF and 0 are the same, pick the right one as 0 in the
	 *	actual field means no checksum.
	 */
	 
	if (uh->check == 0)
		uh->check = 0xffff;
}


static int udp_send(struct sock *sk, struct sockaddr_in *sin,
	 unsigned char *from, int len, int rt)
{
	struct sk_buff *skb;
	struct device *dev;
	struct udphdr *uh;
	unsigned char *buff;
	unsigned long saddr;
	int size, tmp;
	int ttl;
  
	/* 
	 *	Allocate an sk_buff copy of the packet.
	 */
	 
	size = sk->prot->max_header + len;
	skb = sock_alloc_send_skb(sk, size, 0, &tmp);


	if (skb == NULL) 
		return tmp;

	skb->sk       = NULL;	/* to avoid changing sk->saddr */
	skb->free     = 1;
	skb->localroute = sk->localroute|(rt&MSG_DONTROUTE);

	/*
	 *	Now build the IP and MAC header. 
	 */
	 
	buff = skb->data;
	saddr = sk->saddr;
	dev = NULL;
	ttl = sk->ip_ttl;
#ifdef CONFIG_IP_MULTICAST
	if (MULTICAST(sin->sin_addr.s_addr))
		ttl = sk->ip_mc_ttl;
#endif
	tmp = sk->prot->build_header(skb, saddr, sin->sin_addr.s_addr,
			&dev, IPPROTO_UDP, sk->opt, skb->mem_len,sk->ip_tos,ttl);

	skb->sk=sk;	/* So memory is freed correctly */
	
	/*
	 *	Unable to put a header on the packet.
	 */
	 		    
	if (tmp < 0 ) 
	{
		sk->prot->wfree(sk, skb->mem_addr, skb->mem_len);
		return(tmp);
  	}
  	
	buff += tmp;
	saddr = skb->saddr; /*dev->pa_addr;*/
	skb->len = tmp + sizeof(struct udphdr) + len;	/* len + UDP + IP + MAC */
	skb->dev = dev;
	
	/*
	 *	Fill in the UDP header. 
	 */
	 
	uh = (struct udphdr *) buff;
	uh->len = htons(len + sizeof(struct udphdr));
	uh->source = sk->dummy_th.source;
	uh->dest = sin->sin_port;
	buff = (unsigned char *) (uh + 1);

	/*
	 *	Copy the user data. 
	 */
	 
	memcpy_fromfs(buff, from, len);

  	/*
  	 *	Set up the UDP checksum. 
  	 */
  	 
	udp_send_check(uh, saddr, sin->sin_addr.s_addr, skb->len - tmp, sk);

	/* 
	 *	Send the datagram to the interface. 
	 */
	 
	udp_statistics.UdpOutDatagrams++;
	 
	sk->prot->queue_xmit(sk, dev, skb, 1);
	return(len);
}


static int udp_sendto(struct sock *sk, unsigned char *from, int len, int noblock,
	   unsigned flags, struct sockaddr_in *usin, int addr_len)
{
	struct sockaddr_in sin;
	int tmp;

	/* 
	 *	Check the flags. We support no flags for UDP sending
	 */
	if (flags&~MSG_DONTROUTE) 
	  	return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */
	 
	if (usin) 
	{
		if (addr_len < sizeof(sin)) 
			return(-EINVAL);
		memcpy(&sin,usin,sizeof(sin));
		if (sin.sin_family && sin.sin_family != AF_INET) 
			return(-EINVAL);
		if (sin.sin_port == 0) 
			return(-EINVAL);
	} 
	else 
	{
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		sin.sin_family = AF_INET;
		sin.sin_port = sk->dummy_th.dest;
		sin.sin_addr.s_addr = sk->daddr;
  	}
  
  	/*
  	 *	BSD socket semantics. You must set SO_BROADCAST to permit
  	 *	broadcasting of data.
  	 */
  	 
  	if(sin.sin_addr.s_addr==INADDR_ANY)
  		sin.sin_addr.s_addr=ip_my_addr();
  		
  	if(!sk->broadcast && ip_chk_addr(sin.sin_addr.s_addr)==IS_BROADCAST)
	    	return -EACCES;			/* Must turn broadcast on first */

	sk->inuse = 1;

	/* Send the packet. */
	tmp = udp_send(sk, &sin, from, len, flags);

	/* The datagram has been sent off.  Release the socket. */
	release_sock(sk);
	return(tmp);
}

/*
 *	In BSD SOCK_DGRAM a write is just like a send.
 */

static int udp_write(struct sock *sk, unsigned char *buff, int len, int noblock,
	  unsigned flags)
{
	return(udp_sendto(sk, buff, len, noblock, flags, NULL, 0));
}


/*
 *	IOCTL requests applicable to the UDP protocol
 */
 
int udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	switch(cmd) 
	{
		case TIOCOUTQ:
		{
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = sk->prot->wspace(sk)/*/2*/;
			err=verify_area(VERIFY_WRITE,(void *)arg,
					sizeof(unsigned long));
			if(err)
				return(err);
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

		case TIOCINQ:
		{
			struct sk_buff *skb;
			unsigned long amount;

			if (sk->state == TCP_LISTEN) return(-EINVAL);
			amount = 0;
			skb = skb_peek(&sk->receive_queue);
			if (skb != NULL) {
				/*
				 * We will only return the amount
				 * of this packet since that is all
				 * that will be read.
				 */
				amount = skb->len;
			}
			err=verify_area(VERIFY_WRITE,(void *)arg,
						sizeof(unsigned long));
			if(err)
				return(err);
			put_fs_long(amount,(unsigned long *)arg);
			return(0);
		}

		default:
			return(-EINVAL);
	}
	return(0);
}


/*
 * 	This should be easy, if there is something there we\
 * 	return it, otherwise we block.
 */

int udp_recvfrom(struct sock *sk, unsigned char *to, int len,
	     int noblock, unsigned flags, struct sockaddr_in *sin,
	     int *addr_len)
{
  	int copied = 0;
  	int truesize;
  	struct sk_buff *skb;
  	int er;

	/*
	 *	Check any passed addresses
	 */
	 
  	if (addr_len) 
  		*addr_len=sizeof(*sin);
  
	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */
	 	
	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
  		return er;
  
  	truesize = skb->len;
  	copied = min(len, truesize);

  	/*
  	 *	FIXME : should use udp header size info value 
  	 */
  	 
	skb_copy_datagram(skb,sizeof(struct udphdr),to,copied);
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) 
	{
		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
		sin->sin_addr.s_addr = skb->daddr;
  	}
  
  	skb_free_datagram(skb);
  	release_sock(sk);
  	return(truesize);
}

/*
 *	Read has the same semantics as recv in SOCK_DGRAM
 */

int udp_read(struct sock *sk, unsigned char *buff, int len, int noblock,
	 unsigned flags)
{
	return(udp_recvfrom(sk, buff, len, noblock, flags, NULL, NULL));
}


int udp_connect(struct sock *sk, struct sockaddr_in *usin, int addr_len)
{
	struct rtable *rt;
	unsigned long sa;
	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	if (usin->sin_family && usin->sin_family != AF_INET) 
	  	return(-EAFNOSUPPORT);
	if (usin->sin_addr.s_addr==INADDR_ANY)
		usin->sin_addr.s_addr=ip_my_addr();

	if(!sk->broadcast && ip_chk_addr(usin->sin_addr.s_addr)==IS_BROADCAST)
		return -EACCES;			/* Must turn broadcast on first */
  	
  	rt=ip_rt_route(usin->sin_addr.s_addr, NULL, &sa);
  	if(rt==NULL)
  		return -ENETUNREACH;
  	sk->saddr = sa;		/* Update source address */
	sk->daddr = usin->sin_addr.s_addr;
	sk->dummy_th.dest = usin->sin_port;
	sk->state = TCP_ESTABLISHED;
	return(0);
}


static void udp_close(struct sock *sk, int timeout)
{
	sk->inuse = 1;
	sk->state = TCP_CLOSE;
	if (sk->dead) 
		destroy_sock(sk);
	else
		release_sock(sk);
}


/*
 *	All we need to do is get the socket, and then do a checksum. 
 */
 
int udp_rcv(struct sk_buff *skb, struct device *dev, struct options *opt,
	unsigned long daddr, unsigned short len,
	unsigned long saddr, int redo, struct inet_protocol *protocol)
{
  	struct sock *sk;
  	struct udphdr *uh;
	unsigned short ulen;
	int addr_type = IS_MYADDR;
	
	if(!dev || dev->pa_addr!=daddr)
		addr_type=ip_chk_addr(daddr);
		
	/*
	 *	Get the header.
	 */
  	uh = (struct udphdr *) skb->h.uh;
  	
  	ip_statistics.IpInDelivers++;

	/*
	 *	Validate the packet and the UDP length.
	 */
	 
	ulen = ntohs(uh->len);

	if (ulen > len || len < sizeof(*uh) || ulen < sizeof(*uh)) 
	{
		printk("UDP: short packet: %d/%d\n", ulen, len);
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}

	if (uh->check && udp_check(uh, len, saddr, daddr)) 
	{
		/* <mea@utu.fi> wants to know, who sent it, to
		   go and stomp on the garbage sender... */
		printk("UDP: bad checksum. From %08lX:%d to %08lX:%d ulen %d\n",
		       ntohl(saddr),ntohs(uh->source),
		       ntohl(daddr),ntohs(uh->dest),
		       ulen);
		udp_statistics.UdpInErrors++;
		kfree_skb(skb, FREE_WRITE);
		return(0);
	}


	len=ulen;

#ifdef CONFIG_IP_MULTICAST
	if (addr_type!=IS_MYADDR)
	{
		/*
		 *	Multicasts and broadcasts go to each listener.
		 */
		struct sock *sknext=NULL;
		sk=get_sock_mcast(udp_prot.sock_array[ntohs(uh->dest)&(SOCK_ARRAY_SIZE-1)], uh->dest,
				saddr, uh->source, daddr);
		if(sk)
		{		
			do
			{
				struct sk_buff *skb1;

				sknext=get_sock_mcast(sk->next, uh->dest, saddr, uh->source, daddr);
				if(sknext)
					skb1=skb_clone(skb,GFP_ATOMIC);
				else
					skb1=skb;
				if(skb1)
					udp_deliver(sk, uh, skb1, dev,saddr,daddr,len);
				sk=sknext;
			}
			while(sknext!=NULL);
		}
		else
			kfree_skb(skb, FREE_READ);
		return 0;
	}	
#endif
  	sk = get_sock(&udp_prot, uh->dest, saddr, uh->source, daddr);
	if (sk == NULL) 
  	{
  		udp_statistics.UdpNoPorts++;
		if (addr_type == IS_MYADDR) 
		{
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0, dev);
		}
		/*
		 * Hmm.  We got an UDP broadcast to a port to which we
		 * don't wanna listen.  Ignore it.
		 */
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		return(0);
  	}

	return udp_deliver(sk,uh,skb,dev, saddr, daddr, len);
}

static int udp_deliver(struct sock *sk, struct udphdr *uh, struct sk_buff *skb, struct device *dev, long saddr, long daddr, int len)
{
	skb->sk = sk;
	skb->dev = dev;
	skb->len = len;

	/*
	 *	These are supposed to be switched. 
	 */
	 
	skb->daddr = saddr;
	skb->saddr = daddr;


	/*
	 *	Charge it to the socket, dropping if the queue is full.
	 */

	skb->len = len - sizeof(*uh);  
	 
	if (sock_queue_rcv_skb(sk,skb)<0) 
	{
		udp_statistics.UdpInErrors++;
		ip_statistics.IpInDiscards++;
		ip_statistics.IpInDelivers--;
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		release_sock(sk);
		return(0);
	}
  	udp_statistics.UdpInDatagrams++;
	release_sock(sk);
	return(0);
}


struct proto udp_prot = {
	sock_wmalloc,
	sock_rmalloc,
	sock_wfree,
	sock_rfree,
	sock_rspace,
	sock_wspace,
	udp_close,
	udp_read,
	udp_write,
	udp_sendto,
	udp_recvfrom,
	ip_build_header,
	udp_connect,
	NULL,
	ip_queue_xmit,
	NULL,
	NULL,
	NULL,
	udp_rcv,
	datagram_select,
	udp_ioctl,
	NULL,
	NULL,
	ip_setsockopt,
	ip_getsockopt,
	128,
	0,
	{NULL,},
	"UDP",
	0, 0
};

