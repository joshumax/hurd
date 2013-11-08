/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		TIMER - implementation of software timers for IP.
 *
 * Version:	$Id: timer.c,v 1.15 1999/02/22 13:54:29 davem Exp $
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
#include <net/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>

void net_delete_timer (struct sock *t)
{
	if(t->timer.prev)
		del_timer (&t->timer);
	t->timeout = 0;
}

void net_reset_timer (struct sock *t, int timeout, unsigned long len)
{
	t->timeout = timeout;
	mod_timer(&t->timer, jiffies+len);
}

/* Now we will only be called whenever we need to do
 * something, but we must be sure to process all of the
 * sockets that need it.
 */
void net_timer (unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	int why = sk->timeout;

	/* Only process if socket is not in use. */
	if (atomic_read(&sk->sock_readers)) {
		/* Try again later. */ 
		mod_timer(&sk->timer, jiffies+HZ/20);
		return;
	}

	/* Always see if we need to send an ack. */
	if (sk->tp_pinfo.af_tcp.delayed_acks && !sk->zapped) {
		sk->prot->read_wakeup (sk);
		if (!sk->dead)
			sk->data_ready(sk,0);
	}

	/* Now we need to figure out why the socket was on the timer. */
	switch (why) {
		case TIME_DONE:
			/* If the socket hasn't been closed off, re-try a bit later. */
			if (!sk->dead) {
				net_reset_timer(sk, TIME_DONE, TCP_DONE_TIME);
				break;
			}

			if (sk->state != TCP_CLOSE) {
				printk (KERN_DEBUG "non CLOSE socket in time_done\n");
				break;
			}
			destroy_sock (sk);
			break;

		case TIME_DESTROY:
			/* We've waited for a while for all the memory associated with
			 * the socket to be freed.
			 */
			destroy_sock(sk);
			break;

		case TIME_CLOSE:
			/* We've waited long enough, close the socket. */
			tcp_set_state(sk, TCP_CLOSE);
			sk->shutdown = SHUTDOWN_MASK;
			if (!sk->dead)
				sk->state_change(sk);
			net_reset_timer (sk, TIME_DONE, TCP_DONE_TIME);
			break;

		default:
			/* I want to see these... */
			printk ("net_timer: timer expired - reason %d is unknown\n", why);
			break;
	}
}
