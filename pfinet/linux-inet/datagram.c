/*
 *	SUCS NET3:
 *
 *	Generic datagram handling routines. These are generic for all protocols. Possibly a generic IP version on top
 *	of these would make sense. Not tonight however 8-).
 *	This is used because UDP, RAW, PACKET and the to be released IPX layer all have identical select code and mostly
 *	identical recvfrom() code. So we share it here. The select was shared before but buried in udp.c so I moved it.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>. (datagram_select() from old udp.c code)
 *
 *	Fixes:
 *		Alan Cox	:	NULL return from skb_peek_copy() understood
 *		Alan Cox	:	Rewrote skb_read_datagram to avoid the skb_peek_copy stuff.
 *		Alan Cox	:	Added support for SOCK_SEQPACKET. IPX can no longer use the SO_TYPE hack but
 *					AX.25 now works right, and SPX is feasible.
 *		Alan Cox	:	Fixed write select of non IP protocol crash.
 *		Florian  La Roche:	Changed for my new skbuff handling.
 *
 *	Note:
 *		A lot of this will change when the protocol/socket separation
 *	occurs. Using this will make things reasonably clean.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include "route.h"
#include "tcp.h"
#include "udp.h"
#include <linux/skbuff.h>
#include "sock.h"


/*
 *	Get a datagram skbuff, understands the peeking, nonblocking wakeups and possible
 *	races. This replaces identical code in packet,raw and udp, as well as the yet to
 *	be released IPX support. It also finally fixes the long standing peek and read
 *	race for datagram sockets. If you alter this routine remember it must be
 *	re-entrant.
 */

struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags, int noblock, int *err)
{
	struct sk_buff *skb;
	unsigned long intflags;

	/* Socket is inuse - so the timer doesn't attack it */
	save_flags(intflags);
restart:
	sk->inuse = 1;
	while(skb_peek(&sk->receive_queue) == NULL)	/* No data */
	{
		/* If we are shutdown then no more data is going to appear. We are done */
		if (sk->shutdown & RCV_SHUTDOWN)
		{
			release_sock(sk);
			*err=0;
			return NULL;
		}

		if(sk->err)
		{
			release_sock(sk);
			*err=-sk->err;
			sk->err=0;
			return NULL;
		}

		/* Sequenced packets can come disconnected. If so we report the problem */
		if(sk->type==SOCK_SEQPACKET && sk->state!=TCP_ESTABLISHED)
		{
			release_sock(sk);
			*err=-ENOTCONN;
			return NULL;
		}

		/* User doesn't want to wait */
		if (noblock)
		{
			release_sock(sk);
			*err=-EAGAIN;
			return NULL;
		}
		release_sock(sk);

		/* Interrupts off so that no packet arrives before we begin sleeping.
		   Otherwise we might miss our wake up */
		cli();
		if (skb_peek(&sk->receive_queue) == NULL)
		{
			interruptible_sleep_on(sk->sleep);
			/* Signals may need a restart of the syscall */
			if (current->signal & ~current->blocked)
			{
				restore_flags(intflags);;
				*err=-ERESTARTSYS;
				return(NULL);
			}
			if(sk->err != 0)	/* Error while waiting for packet
						   eg an icmp sent earlier by the
						   peer has finally turned up now */
			{
				*err = -sk->err;
				sk->err=0;
				restore_flags(intflags);
				return NULL;
			}
		}
		sk->inuse = 1;
		restore_flags(intflags);
	  }
	  /* Again only user level code calls this function, so nothing interrupt level
	     will suddenly eat the receive_queue */
	  if (!(flags & MSG_PEEK))
	  {
		skb=skb_dequeue(&sk->receive_queue);
		if(skb!=NULL)
			skb->users++;
		else
			goto restart;	/* Avoid race if someone beats us to the data */
	  }
	  else
	  {
		cli();
		skb=skb_peek(&sk->receive_queue);
		if(skb!=NULL)
			skb->users++;
		restore_flags(intflags);
		if(skb==NULL)	/* shouldn't happen but .. */
			*err=-EAGAIN;
	  }
	  return skb;
}

void skb_free_datagram(struct sk_buff *skb)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	skb->users--;
	if(skb->users>0)
	{
		restore_flags(flags);
		return;
	}
	/* See if it needs destroying */
	if(!skb->next && !skb->prev)	/* Been dequeued by someone - ie it's read */
		kfree_skb(skb,FREE_READ);
	restore_flags(flags);
}

void skb_copy_datagram(struct sk_buff *skb, int offset, char *to, int size)
{
	/* We will know all about the fraglist options to allow >4K receives
	   but not this release */
	memcpy_tofs(to,skb->h.raw+offset,size);
}

/*
 *	Datagram select: Again totally generic. Moved from udp.c
 *	Now does seqpacket.
 */

int datagram_select(struct sock *sk, int sel_type, select_table *wait)
{
	select_wait(sk->sleep, wait);
	switch(sel_type)
	{
		case SEL_IN:
			if (sk->type==SOCK_SEQPACKET && sk->state==TCP_CLOSE)
			{
				/* Connection closed: Wake up */
				return(1);
			}
			if (skb_peek(&sk->receive_queue) != NULL || sk->err != 0)
			{	/* This appears to be consistent
				   with other stacks */
				return(1);
			}
			return(0);

		case SEL_OUT:
			if (sk->prot && sk->prot->wspace(sk) >= MIN_WRITE_SPACE)
			{
				return(1);
			}
			if (sk->prot==NULL && sk->sndbuf-sk->wmem_alloc >= MIN_WRITE_SPACE)
			{
				return(1);
			}
			return(0);

		case SEL_EX:
			if (sk->err)
				return(1); /* Socket has gone into error state (eg icmp error) */
			return(0);
	}
	return(0);
}
