/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		TIMER - implementation of software timers for IP.
 *
 * Version:	@(#)timer.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	To avoid destroying a wait queue as we use it
 *					we defer destruction until the destroy timer goes
 *					off.
 *		Alan Cox	:	Destroy socket doesn't write a status value to the
 *					socket buffer _AFTER_ freeing it! Also sock ensures
 *					the socket will get removed BEFORE this is called
 *					otherwise if the timer TIME_DESTROY occurs inside
 *					of inet_bh() with this socket being handled it goes
 *					BOOM! Have to stop timer going off if net_bh is
 *					active or the destroy causes crashes.
 *		Alan Cox	:	Cleaned up unused code.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <linux/interrupt.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"

void delete_timer (struct sock *t)
{
	unsigned long flags;

	save_flags (flags);
	cli();

	t->timeout = 0;
	del_timer (&t->timer);

	restore_flags (flags);
}

void reset_timer (struct sock *t, int timeout, unsigned long len)
{
	delete_timer (t);
	t->timeout = timeout;
#if 1
  /* FIXME: ??? */
	if ((int) len < 0)	/* prevent close to infinite timers. THEY _DO_ */
		len = 3;	/* happen (negative values ?) - don't ask me why ! -FB */
#endif
	t->timer.expires = len;
	add_timer (&t->timer);
}


/*
 *	Now we will only be called whenever we need to do
 *	something, but we must be sure to process all of the
 *	sockets that need it.
 */

void net_timer (unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	int why = sk->timeout;

	/* 
	 * only process if socket is not in use
	 */

	cli();
	if (sk->inuse || in_bh) 
	{
		sk->timer.expires = 10;
		add_timer(&sk->timer);
		sti();
		return;
	}

	sk->inuse = 1;
	sti();

	/* Always see if we need to send an ack. */

	if (sk->ack_backlog && !sk->zapped) 
	{
		sk->prot->read_wakeup (sk);
		if (! sk->dead)
		sk->data_ready(sk,0);
	}

	/* Now we need to figure out why the socket was on the timer. */

	switch (why) 
	{
		case TIME_DONE:
			if (! sk->dead || sk->state != TCP_CLOSE) 
			{
				printk ("non dead socket in time_done\n");
				release_sock (sk);
				break;
			}
			destroy_sock (sk);
			break;

		case TIME_DESTROY:
		/*
		 *	We've waited for a while for all the memory associated with
		 *	the socket to be freed.
		 */
			if(sk->wmem_alloc!=0 || sk->rmem_alloc!=0)
			{
				sk->wmem_alloc++;	/* So it DOESN'T go away */
				destroy_sock (sk);
				sk->wmem_alloc--;	/* Might now have hit 0 - fall through and do it again if so */
				sk->inuse = 0;	/* This will be ok, the destroy won't totally work */
			}
			if(sk->wmem_alloc==0 && sk->rmem_alloc==0)
				destroy_sock(sk);	/* Socket gone, DON'T update sk->inuse! */
				break;
		case TIME_CLOSE:
			/* We've waited long enough, close the socket. */
			sk->state = TCP_CLOSE;
			delete_timer (sk);
			/* Kill the ARP entry in case the hardware has changed. */
			arp_destroy (sk->daddr, 0);
			if (!sk->dead)
				sk->state_change(sk);
			sk->shutdown = SHUTDOWN_MASK;
			reset_timer (sk, TIME_DESTROY, TCP_DONE_TIME);
			release_sock (sk);
			break;
#if 0
		case TIME_PROBE0:
			tcp_send_probe0(sk);
			release_sock (sk);
			break;
		case TIME_WRITE:	/* try to retransmit. */
			/* It could be we got here because we needed to send an ack.
			 * So we need to check for that.
			 */
		{
			struct sk_buff *skb;
			unsigned long flags;

			save_flags(flags);
			cli();
			skb = sk->send_head;
			if (!skb) 
			{
				restore_flags(flags);
			} 
			else 
			{
				if (jiffies < skb->when + sk->rto) 
				{
					reset_timer (sk, TIME_WRITE, skb->when + sk->rto - jiffies);
					restore_flags(flags);
					release_sock (sk);
					break;
				}
				restore_flags(flags);
				/* printk("timer: seq %d retrans %d out %d cong %d\n", sk->send_head->h.seq,
					sk->retransmits, sk->packets_out, sk->cong_window); */
				sk->prot->retransmit (sk, 0);
				if ((sk->state == TCP_ESTABLISHED && sk->retransmits && !(sk->retransmits & 7))
					|| (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR1)) 
				{
					arp_destroy (sk->daddr, 0);
					ip_route_check (sk->daddr);
				}
				if (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR2) 
				{
					sk->err = ETIMEDOUT;
					if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2 || sk->state == TCP_CLOSING) 
					{
						sk->state = TCP_TIME_WAIT;
						reset_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
					}
					else
					{
						sk->prot->close (sk, 1);
							break;
					}
				}
			}
			release_sock (sk);
			break;
		}
		case TIME_KEEPOPEN:
			/* 
			 * this reset_timer() call is a hack, this is not
			 * how KEEPOPEN is supposed to work.
			 */
			reset_timer (sk, TIME_KEEPOPEN, TCP_TIMEOUT_LEN);

			/* Send something to keep the connection open. */
			if (sk->prot->write_wakeup)
				  sk->prot->write_wakeup (sk);
			sk->retransmits++;
			if (sk->shutdown == SHUTDOWN_MASK) 
			{
				sk->prot->close (sk, 1);
				sk->state = TCP_CLOSE;
			}
			if ((sk->state == TCP_ESTABLISHED && sk->retransmits && !(sk->retransmits & 7))
				|| (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR1)) 
			{
				arp_destroy (sk->daddr, 0);
				ip_route_check (sk->daddr);
				release_sock (sk);
				break;
			}
			if (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR2) 
			{
				arp_destroy (sk->daddr, 0);
				sk->err = ETIMEDOUT;
				if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2) 
				{
					sk->state = TCP_TIME_WAIT;
					if (!sk->dead)
						sk->state_change(sk);
					release_sock (sk);
				  } 
				  else 
				  {
					sk->prot->close (sk, 1);
				  }
				  break;
			}
			release_sock (sk);
			break;
#endif
		default:
			printk ("net_timer: timer expired - reason %d is unknown\n", why);
			release_sock (sk);
			break;
	}
}

