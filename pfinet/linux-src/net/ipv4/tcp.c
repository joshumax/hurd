/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp.c,v 1.140.2.5 1999/09/23 19:21:16 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 *
 * Fixes:
 *		Alan Cox	:	Numerous verify_area() calls
 *		Alan Cox	:	Set the ACK bit on a reset
 *		Alan Cox	:	Stopped it crashing if it closed while
 *					sk->inuse=1 and was trying to connect
 *					(tcp_err()).
 *		Alan Cox	:	All icmp error handling was broken
 *					pointers passed where wrong and the
 *					socket was looked up backwards. Nobody
 *					tested any icmp error code obviously.
 *		Alan Cox	:	tcp_err() now handled properly. It
 *					wakes people on errors. poll
 *					behaves and the icmp error race
 *					has gone by moving it into sock.c
 *		Alan Cox	:	tcp_send_reset() fixed to work for
 *					everything not just packets for
 *					unknown sockets.
 *		Alan Cox	:	tcp option processing.
 *		Alan Cox	:	Reset tweaked (still not 100%) [Had
 *					syn rule wrong]
 *		Herp Rosmanith  :	More reset fixes
 *		Alan Cox	:	No longer acks invalid rst frames.
 *					Acking any kind of RST is right out.
 *		Alan Cox	:	Sets an ignore me flag on an rst
 *					receive otherwise odd bits of prattle
 *					escape still
 *		Alan Cox	:	Fixed another acking RST frame bug.
 *					Should stop LAN workplace lockups.
 *		Alan Cox	: 	Some tidyups using the new skb list
 *					facilities
 *		Alan Cox	:	sk->keepopen now seems to work
 *		Alan Cox	:	Pulls options out correctly on accepts
 *		Alan Cox	:	Fixed assorted sk->rqueue->next errors
 *		Alan Cox	:	PSH doesn't end a TCP read. Switched a
 *					bit to skb ops.
 *		Alan Cox	:	Tidied tcp_data to avoid a potential
 *					nasty.
 *		Alan Cox	:	Added some better commenting, as the
 *					tcp is hard to follow
 *		Alan Cox	:	Removed incorrect check for 20 * psh
 *	Michael O'Reilly	:	ack < copied bug fix.
 *	Johannes Stille		:	Misc tcp fixes (not all in yet).
 *		Alan Cox	:	FIN with no memory -> CRASH
 *		Alan Cox	:	Added socket option proto entries.
 *					Also added awareness of them to accept.
 *		Alan Cox	:	Added TCP options (SOL_TCP)
 *		Alan Cox	:	Switched wakeup calls to callbacks,
 *					so the kernel can layer network
 *					sockets.
 *		Alan Cox	:	Use ip_tos/ip_ttl settings.
 *		Alan Cox	:	Handle FIN (more) properly (we hope).
 *		Alan Cox	:	RST frames sent on unsynchronised
 *					state ack error.
 *		Alan Cox	:	Put in missing check for SYN bit.
 *		Alan Cox	:	Added tcp_select_window() aka NET2E
 *					window non shrink trick.
 *		Alan Cox	:	Added a couple of small NET2E timer
 *					fixes
 *		Charles Hedrick :	TCP fixes
 *		Toomas Tamm	:	TCP window fixes
 *		Alan Cox	:	Small URG fix to rlogin ^C ack fight
 *		Charles Hedrick	:	Rewrote most of it to actually work
 *		Linus		:	Rewrote tcp_read() and URG handling
 *					completely
 *		Gerhard Koerting:	Fixed some missing timer handling
 *		Matthew Dillon  :	Reworked TCP machine states as per RFC
 *		Gerhard Koerting:	PC/TCP workarounds
 *		Adam Caldwell	:	Assorted timer/timing errors
 *		Matthew Dillon	:	Fixed another RST bug
 *		Alan Cox	:	Move to kernel side addressing changes.
 *		Alan Cox	:	Beginning work on TCP fastpathing
 *					(not yet usable)
 *		Arnt Gulbrandsen:	Turbocharged tcp_check() routine.
 *		Alan Cox	:	TCP fast path debugging
 *		Alan Cox	:	Window clamping
 *		Michael Riepe	:	Bug in tcp_check()
 *		Matt Dillon	:	More TCP improvements and RST bug fixes
 *		Matt Dillon	:	Yet more small nasties remove from the
 *					TCP code (Be very nice to this man if
 *					tcp finally works 100%) 8)
 *		Alan Cox	:	BSD accept semantics.
 *		Alan Cox	:	Reset on closedown bug.
 *	Peter De Schrijver	:	ENOTCONN check missing in tcp_sendto().
 *		Michael Pall	:	Handle poll() after URG properly in
 *					all cases.
 *		Michael Pall	:	Undo the last fix in tcp_read_urg()
 *					(multi URG PUSH broke rlogin).
 *		Michael Pall	:	Fix the multi URG PUSH problem in
 *					tcp_readable(), poll() after URG
 *					works now.
 *		Michael Pall	:	recv(...,MSG_OOB) never blocks in the
 *					BSD api.
 *		Alan Cox	:	Changed the semantics of sk->socket to
 *					fix a race and a signal problem with
 *					accept() and async I/O.
 *		Alan Cox	:	Relaxed the rules on tcp_sendto().
 *		Yury Shevchuk	:	Really fixed accept() blocking problem.
 *		Craig I. Hagan  :	Allow for BSD compatible TIME_WAIT for
 *					clients/servers which listen in on
 *					fixed ports.
 *		Alan Cox	:	Cleaned the above up and shrank it to
 *					a sensible code size.
 *		Alan Cox	:	Self connect lockup fix.
 *		Alan Cox	:	No connect to multicast.
 *		Ross Biro	:	Close unaccepted children on master
 *					socket close.
 *		Alan Cox	:	Reset tracing code.
 *		Alan Cox	:	Spurious resets on shutdown.
 *		Alan Cox	:	Giant 15 minute/60 second timer error
 *		Alan Cox	:	Small whoops in polling before an
 *					accept.
 *		Alan Cox	:	Kept the state trace facility since
 *					it's handy for debugging.
 *		Alan Cox	:	More reset handler fixes.
 *		Alan Cox	:	Started rewriting the code based on
 *					the RFC's for other useful protocol
 *					references see: Comer, KA9Q NOS, and
 *					for a reference on the difference
 *					between specifications and how BSD
 *					works see the 4.4lite source.
 *		A.N.Kuznetsov	:	Don't time wait on completion of tidy
 *					close.
 *		Linus Torvalds	:	Fin/Shutdown & copied_seq changes.
 *		Linus Torvalds	:	Fixed BSD port reuse to work first syn
 *		Alan Cox	:	Reimplemented timers as per the RFC
 *					and using multiple timers for sanity.
 *		Alan Cox	:	Small bug fixes, and a lot of new
 *					comments.
 *		Alan Cox	:	Fixed dual reader crash by locking
 *					the buffers (much like datagram.c)
 *		Alan Cox	:	Fixed stuck sockets in probe. A probe
 *					now gets fed up of retrying without
 *					(even a no space) answer.
 *		Alan Cox	:	Extracted closing code better
 *		Alan Cox	:	Fixed the closing state machine to
 *					resemble the RFC.
 *		Alan Cox	:	More 'per spec' fixes.
 *		Jorge Cwik	:	Even faster checksumming.
 *		Alan Cox	:	tcp_data() doesn't ack illegal PSH
 *					only frames. At least one pc tcp stack
 *					generates them.
 *		Alan Cox	:	Cache last socket.
 *		Alan Cox	:	Per route irtt.
 *		Matt Day	:	poll()->select() match BSD precisely on error
 *		Alan Cox	:	New buffers
 *		Marc Tamsky	:	Various sk->prot->retransmits and
 *					sk->retransmits misupdating fixed.
 *					Fixed tcp_write_timeout: stuck close,
 *					and TCP syn retries gets used now.
 *		Mark Yarvis	:	In tcp_read_wakeup(), don't send an
 *					ack if state is TCP_CLOSED.
 *		Alan Cox	:	Look up device on a retransmit - routes may
 *					change. Doesn't yet cope with MSS shrink right
 *					but its a start!
 *		Marc Tamsky	:	Closing in closing fixes.
 *		Mike Shaver	:	RFC1122 verifications.
 *		Alan Cox	:	rcv_saddr errors.
 *		Alan Cox	:	Block double connect().
 *		Alan Cox	:	Small hooks for enSKIP.
 *		Alexey Kuznetsov:	Path MTU discovery.
 *		Alan Cox	:	Support soft errors.
 *		Alan Cox	:	Fix MTU discovery pathological case
 *					when the remote claims no mtu!
 *		Marc Tamsky	:	TCP_CLOSE fix.
 *		Colin (G3TNE)	:	Send a reset on syn ack replies in
 *					window but wrong (fixes NT lpd problems)
 *		Pedro Roque	:	Better TCP window handling, delayed ack.
 *		Joerg Reuter	:	No modification of locked buffers in
 *					tcp_do_retransmit()
 *		Eric Schenk	:	Changed receiver side silly window
 *					avoidance algorithm to BSD style
 *					algorithm. This doubles throughput
 *					against machines running Solaris,
 *					and seems to result in general
 *					improvement.
 *	Stefan Magdalinski	:	adjusted tcp_readable() to fix FIONREAD
 *	Willy Konynenberg	:	Transparent proxying support.
 *	Mike McLagan		:	Routing by source
 *		Keith Owens	:	Do proper merging with partial SKB's in
 *					tcp_do_sendmsg to avoid burstiness.
 *		Eric Schenk	:	Fix fast close down bug with
 *					shutdown() followed by close().
 *		Andi Kleen :	Make poll agree with SIGIO
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or(at your option) any later version.
 *
 * Description of States:
 *
 *	TCP_SYN_SENT		sent a connection request, waiting for ack
 *
 *	TCP_SYN_RECV		received a connection request, sent ack,
 *				waiting for final ack in three-way handshake.
 *
 *	TCP_ESTABLISHED		connection established
 *
 *	TCP_FIN_WAIT1		our side has shutdown, waiting to complete
 *				transmission of remaining buffered data
 *
 *	TCP_FIN_WAIT2		all buffered data sent, waiting for remote
 *				to shutdown
 *
 *	TCP_CLOSING		both sides have shutdown but we still have
 *				data we have to finish sending
 *
 *	TCP_TIME_WAIT		timeout to catch resent junk before entering
 *				closed, can only be entered from FIN_WAIT2
 *				or CLOSING.  Required because the other end
 *				may not have gotten our last ACK causing it
 *				to retransmit the data packet (which we ignore)
 *
 *	TCP_CLOSE_WAIT		remote side has shutdown and is waiting for
 *				us to finish writing our data and to shutdown
 *				(we have to close() to move on to LAST_ACK)
 *
 *	TCP_LAST_ACK		out side has shutdown after remote has
 *				shutdown.  There may still be data in our
 *				buffer that we have to finish sending
 *
 *	TCP_CLOSE		socket is finished
 */

/*
 * RFC1122 status:
 * NOTE: I'm not going to be doing comments in the code for this one except
 * for violations and the like.  tcp.c is just too big... If I say something
 * "does?" or "doesn't?", it means I'm not sure, and will have to hash it out
 * with Alan. -- MS 950903
 * [Note: Most of the TCP code has been rewriten/redesigned since this
 *  RFC1122 check. It is probably not correct anymore. It should be redone
 *  before 2.2. -AK]
 *
 * Use of PSH (4.2.2.2)
 *   MAY aggregate data sent without the PSH flag. (does)
 *   MAY queue data received without the PSH flag. (does)
 *   SHOULD collapse successive PSH flags when it packetizes data. (doesn't)
 *   MAY implement PSH on send calls. (doesn't, thus:)
 *     MUST NOT buffer data indefinitely (doesn't [1 second])
 *     MUST set PSH on last segment (does)
 *   MAY pass received PSH to application layer (doesn't)
 *   SHOULD send maximum-sized segment whenever possible. (almost always does)
 *
 * Window Size (4.2.2.3, 4.2.2.16)
 *   MUST treat window size as an unsigned number (does)
 *   SHOULD treat window size as a 32-bit number (does not)
 *   MUST NOT shrink window once it is offered (does not normally)
 *
 * Urgent Pointer (4.2.2.4)
 * **MUST point urgent pointer to last byte of urgent data (not right
 *     after). (doesn't, to be like BSD. That's configurable, but defaults
 *	to off)
 *   MUST inform application layer asynchronously of incoming urgent
 *     data. (does)
 *   MUST provide application with means of determining the amount of
 *     urgent data pending. (does)
 * **MUST support urgent data sequence of arbitrary length. (doesn't, but
 *   it's sort of tricky to fix, as urg_ptr is a 16-bit quantity)
 *	[Follows BSD 1 byte of urgent data]
 *
 * TCP Options (4.2.2.5)
 *   MUST be able to receive TCP options in any segment. (does)
 *   MUST ignore unsupported options (does)
 *
 * Maximum Segment Size Option (4.2.2.6)
 *   MUST implement both sending and receiving MSS. (does, but currently
 *	only uses the smaller of both of them)
 *   SHOULD send an MSS with every SYN where receive MSS != 536 (MAY send
 *     it always). (does, even when MSS == 536, which is legal)
 *   MUST assume MSS == 536 if no MSS received at connection setup (does)
 *   MUST calculate "effective send MSS" correctly:
 *     min(physical_MTU, remote_MSS+20) - sizeof(tcphdr) - sizeof(ipopts)
 *     (does - but allows operator override)
 *
 * TCP Checksum (4.2.2.7)
 *   MUST generate and check TCP checksum. (does)
 *
 * Initial Sequence Number Selection (4.2.2.8)
 *   MUST use the RFC 793 clock selection mechanism.  (doesn't, but it's
 *     OK: RFC 793 specifies a 250KHz clock, while we use 1MHz, which is
 *     necessary for 10Mbps networks - and harder than BSD to spoof!
 *     With syncookies we don't)
 *
 * Simultaneous Open Attempts (4.2.2.10)
 *   MUST support simultaneous open attempts (does)
 *
 * Recovery from Old Duplicate SYN (4.2.2.11)
 *   MUST keep track of active vs. passive open (does)
 *
 * RST segment (4.2.2.12)
 *   SHOULD allow an RST segment to contain data (does, but doesn't do
 *     anything with it, which is standard)
 *
 * Closing a Connection (4.2.2.13)
 *   MUST inform application of whether connection was closed by RST or
 *     normal close. (does)
 *   MAY allow "half-duplex" close (treat connection as closed for the
 *     local app, even before handshake is done). (does)
 *   MUST linger in TIME_WAIT for 2 * MSL (does)
 *
 * Retransmission Timeout (4.2.2.15)
 *   MUST implement Jacobson's slow start and congestion avoidance
 *     stuff. (does)
 *
 * Probing Zero Windows (4.2.2.17)
 *   MUST support probing of zero windows. (does)
 *   MAY keep offered window closed indefinitely. (does)
 *   MUST allow remote window to stay closed indefinitely. (does)
 *
 * Passive Open Calls (4.2.2.18)
 *   MUST NOT let new passive open affect other connections. (doesn't)
 *   MUST support passive opens (LISTENs) concurrently. (does)
 *
 * Time to Live (4.2.2.19)
 *   MUST make TCP TTL configurable. (does - IP_TTL option)
 *
 * Event Processing (4.2.2.20)
 *   SHOULD queue out-of-order segments. (does)
 *   MUST aggregate ACK segments whenever possible. (does but badly)
 *
 * Retransmission Timeout Calculation (4.2.3.1)
 *   MUST implement Karn's algorithm and Jacobson's algorithm for RTO
 *     calculation. (does, or at least explains them in the comments 8*b)
 *  SHOULD initialize RTO to 0 and RTT to 3. (does)
 *
 * When to Send an ACK Segment (4.2.3.2)
 *   SHOULD implement delayed ACK. (does)
 *   MUST keep ACK delay < 0.5 sec. (does)
 *
 * When to Send a Window Update (4.2.3.3)
 *   MUST implement receiver-side SWS. (does)
 *
 * When to Send Data (4.2.3.4)
 *   MUST implement sender-side SWS. (does)
 *   SHOULD implement Nagle algorithm. (does)
 *
 * TCP Connection Failures (4.2.3.5)
 *  MUST handle excessive retransmissions "properly" (see the RFC). (does)
 *   SHOULD inform application layer of soft errors. (does)
 *
 * TCP Keep-Alives (4.2.3.6)
 *   MAY provide keep-alives. (does)
 *   MUST make keep-alives configurable on a per-connection basis. (does)
 *   MUST default to no keep-alives. (does)
 *   MUST make keep-alive interval configurable. (does)
 *   MUST make default keep-alive interval > 2 hours. (does)
 *   MUST NOT interpret failure to ACK keep-alive packet as dead
 *     connection. (doesn't)
 *   SHOULD send keep-alive with no data. (does)
 *
 * TCP Multihoming (4.2.3.7)
 *   MUST get source address from IP layer before sending first
 *     SYN. (does)
 *   MUST use same local address for all segments of a connection. (does)
 *
 * IP Options (4.2.3.8)
 *   MUST ignore unsupported IP options. (does)
 *   MAY support Time Stamp and Record Route. (does)
 *   MUST allow application to specify a source route. (does)
 *   MUST allow received Source Route option to set route for all future
 *     segments on this connection. (does not (security issues))
 *
 * ICMP messages (4.2.3.9)
 *   MUST act on ICMP errors. (does)
 *   MUST slow transmission upon receipt of a Source Quench. (doesn't anymore
 *   because that is deprecated now by the IETF, can be turned on)
 *   MUST NOT abort connection upon receipt of soft Destination
 *     Unreachables (0, 1, 5), Time Exceededs and Parameter
 *     Problems. (doesn't)
 *   SHOULD report soft Destination Unreachables etc. to the
 *     application. (does, except during SYN_RECV and may drop messages
 *     in some rare cases before accept() - ICMP is unreliable)
 *   SHOULD abort connection upon receipt of hard Destination Unreachable
 *     messages (2, 3, 4). (does, but see above)
 *
 * Remote Address Validation (4.2.3.10)
 *   MUST reject as an error OPEN for invalid remote IP address. (does)
 *   MUST ignore SYN with invalid source address. (does)
 *   MUST silently discard incoming SYN for broadcast/multicast
 *     address. (does)
 *
 * Asynchronous Reports (4.2.4.1)
 * MUST provide mechanism for reporting soft errors to application
 *     layer. (does)
 *
 * Type of Service (4.2.4.2)
 *   MUST allow application layer to set Type of Service. (does IP_TOS)
 *
 * (Whew. -- MS 950903)
 * (Updated by AK, but not complete yet.)
 **/

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <net/icmp.h>
#include <net/tcp.h>

#include <asm/uaccess.h>

int sysctl_tcp_fin_timeout = TCP_FIN_TIMEOUT;

struct tcp_mib	tcp_statistics;

kmem_cache_t *tcp_openreq_cachep;
kmem_cache_t *tcp_bucket_cachep;
kmem_cache_t *tcp_timewait_cachep;

/*
 *	Find someone to 'accept'. Must be called with
 *	the socket locked or with interrupts disabled
 */

static struct open_request *tcp_find_established(struct tcp_opt *tp,
						 struct open_request **prevp)
{
	struct open_request *req = tp->syn_wait_queue;
	struct open_request *prev = (struct open_request *)&tp->syn_wait_queue;
	while(req) {
		if (req->sk &&
		    ((1 << req->sk->state) &
		     ~(TCPF_SYN_SENT|TCPF_SYN_RECV)))
			break;
		prev = req;
		req = req->dl_next;
	}
	*prevp = prev;
	return req;
}

/*
 *	Walk down the receive queue counting readable data.
 *
 *	Must be called with the socket lock held.
 */

static int tcp_readable(struct sock *sk)
{
	unsigned long counted;
	unsigned long amount;
	struct sk_buff *skb;
	int sum;

	SOCK_DEBUG(sk, "tcp_readable: %p - ",sk);

	skb = skb_peek(&sk->receive_queue);
	if (skb == NULL) {
		SOCK_DEBUG(sk, "empty\n");
	  	return(0);
	}

	counted = sk->tp_pinfo.af_tcp.copied_seq;	/* Where we are at the moment */
	amount = 0;

	/* Do until a push or until we are out of data. */
	do {
		/* Found a hole so stops here. */
		if (before(counted, TCP_SKB_CB(skb)->seq))	/* should not happen */
			break;

		/* Length - header but start from where we are up to
		 * avoid overlaps.
		 */
		sum = skb->len - (counted - TCP_SKB_CB(skb)->seq);
		if (sum >= 0) {
			/* Add it up, move on. */
			amount += sum;
			counted += sum;
			if (skb->h.th->syn)
				counted++;
		}

		/* Don't count urg data ... but do it in the right place!
		 * Consider: "old_data (ptr is here) URG PUSH data"
		 * The old code would stop at the first push because
		 * it counted the urg (amount==1) and then does amount--
		 * *after* the loop.  This means tcp_readable() always
		 * returned zero if any URG PUSH was in the queue, even
		 * though there was normal data available. If we subtract
		 * the urg data right here, we even get it to work for more
		 * than one URG PUSH skb without normal data.
		 * This means that poll() finally works now with urg data
		 * in the queue.  Note that rlogin was never affected
		 * because it doesn't use poll(); it uses two processes
		 * and a blocking read().  And the queue scan in tcp_read()
		 * was correct.  Mike <pall@rz.uni-karlsruhe.de>
		 */

		/* Don't count urg data. */
		if (skb->h.th->urg)
			amount--;
#if 0
		if (amount && skb->h.th->psh) break;
#endif
		skb = skb->next;
	} while(skb != (struct sk_buff *)&sk->receive_queue);

	SOCK_DEBUG(sk, "got %lu bytes.\n",amount);
	return(amount);
}

/*
 * LISTEN is a special case for poll..
 */
static unsigned int tcp_listen_poll(struct sock *sk, poll_table *wait)
{
	struct open_request *req, *dummy;

	lock_sock(sk);
	req = tcp_find_established(&sk->tp_pinfo.af_tcp, &dummy);
	release_sock(sk);
	if (req)
		return POLLIN | POLLRDNORM;
	return 0;
}

/*
 *	Compute minimal free write space needed to queue new packets.
 */
#define tcp_min_write_space(__sk) \
	(atomic_read(&(__sk)->wmem_alloc) / 2)

/*
 *	Wait for a TCP event.
 *
 *	Note that we don't need to lock the socket, as the upper poll layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
unsigned int tcp_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	poll_wait(file, sk->sleep, wait);
	if (sk->state == TCP_LISTEN)
		return tcp_listen_poll(sk, wait);

	mask = 0;
	if (sk->err)
		mask = POLLERR;

	/*
	 * POLLHUP is certainly not done right. But poll() doesn't
	 * have a notion of HUP in just one direction, and for a
	 * socket the read side is more interesting.
	 *
	 * Some poll() documentation says that POLLHUP is incompatible
	 * with the POLLOUT/POLLWR flags, so somebody should check this
	 * all. But careful, it tends to be safer to return too many
	 * bits than too few, and you can easily break real applications
	 * if you don't tell them that something has hung up!
	 *
	 * Check-me.
	 */
	if (sk->shutdown & RCV_SHUTDOWN)
		mask |= POLLHUP;

	/* Connected? */
	if ((1 << sk->state) & ~(TCPF_SYN_SENT|TCPF_SYN_RECV)) {
		if ((tp->rcv_nxt != tp->copied_seq) &&
		    (tp->urg_seq != tp->copied_seq ||
		     tp->rcv_nxt != tp->copied_seq+1 ||
		     sk->urginline || !tp->urg_data))
			mask |= POLLIN | POLLRDNORM;

		if (!(sk->shutdown & SEND_SHUTDOWN)) {
			if (sock_wspace(sk) >= tcp_min_write_space(sk)) {
				mask |= POLLOUT | POLLWRNORM;
			} else {  /* send SIGIO later */
				sk->socket->flags |= SO_NOSPACE;
			}
		}

		if (tp->urg_data & URG_VALID)
			mask |= POLLPRI;
	}
	return mask;
}

/*
 *	Socket write_space callback.
 *	This (or rather the sock_wake_async) should agree with poll.
 */
void tcp_write_space(struct sock *sk)
{
	if (sk->dead)
		return;

	wake_up_interruptible(sk->sleep);
	if (sock_wspace(sk) >=
	    tcp_min_write_space(sk))
		sock_wake_async(sk->socket, 2);
}


#ifdef _HURD_

#define tcp_ioctl 0

error_t
tcp_tiocinq(struct sock *sk, mach_msg_type_number_t *amount)
{
  if (sk->state == TCP_LISTEN)
    return EINVAL;
  lock_sock(sk);
  *amount = tcp_readable(sk);
  release_sock(sk);
  return 0;
}

#else

int tcp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int answ;

	switch(cmd) {
	case TIOCINQ:
#ifdef FIXME	/* FIXME: */
	case FIONREAD:
#endif
		if (sk->state == TCP_LISTEN)
			return(-EINVAL);
		lock_sock(sk);
		answ = tcp_readable(sk);
		release_sock(sk);
		break;
	case SIOCATMARK:
		{
			struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
			answ = tp->urg_data && tp->urg_seq == tp->copied_seq;
			break;
		}
	case TIOCOUTQ:
		if (sk->state == TCP_LISTEN)
			return(-EINVAL);
		answ = sock_wspace(sk);
		break;
	default:
		return(-ENOIOCTLCMD);
	};

	return put_user(answ, (int *)arg);
}

#endif

/*
 *	Wait for a socket to get into the connected state
 *
 *	Note: must be called with the socket locked.
 */
static int wait_for_tcp_connect(struct sock * sk, int flags)
{
	struct task_struct *tsk = current;
	struct wait_queue wait = { tsk, NULL };

	while((1 << sk->state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)) {
		if(sk->err)
			return sock_error(sk);
		if((1 << sk->state) &
		   ~(TCPF_SYN_SENT | TCPF_SYN_RECV)) {
			if(sk->keepopen && !(flags&MSG_NOSIGNAL))
				send_sig(SIGPIPE, tsk, 0);
			return -EPIPE;
		}
		if(flags & MSG_DONTWAIT)
			return -EAGAIN;
		if(signal_pending(tsk))
			return -ERESTARTSYS;

		tsk->state = TASK_INTERRUPTIBLE;
		add_wait_queue(sk->sleep, &wait);
		release_sock(sk);

		if (((1 << sk->state) & ~(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT)) &&
		    sk->err == 0)
			schedule();

		tsk->state = TASK_RUNNING;
		remove_wait_queue(sk->sleep, &wait);
		lock_sock(sk);
	}
	return 0;
}

static inline int tcp_memory_free(struct sock *sk)
{
	return atomic_read(&sk->wmem_alloc) < sk->sndbuf;
}

/*
 *	Wait for more memory for a socket
 */
static void wait_for_tcp_memory(struct sock * sk)
{
	release_sock(sk);
	if (!tcp_memory_free(sk)) {
		struct wait_queue wait = { current, NULL };

		sk->socket->flags &= ~SO_NOSPACE;
		add_wait_queue(sk->sleep, &wait);
		for (;;) {
			if (signal_pending(current))
				break;
			current->state = TASK_INTERRUPTIBLE;
			if (tcp_memory_free(sk))
				break;
			if (sk->shutdown & SEND_SHUTDOWN)
				break;
			if (sk->err)
				break;
			schedule();
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(sk->sleep, &wait);
	}
	lock_sock(sk);
}

/*
 * Wait for a buffer.
 */
static int wait_for_buffer(struct sock *sk)
{
	struct wait_queue wait = { current, NULL };

	release_sock(sk);
	add_wait_queue(sk->sleep, &wait);
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);
	lock_sock(sk);
	return 0;
}

/* When all user supplied data has been queued set the PSH bit */
#define PSH_NEEDED (seglen == 0 && iovlen == 0)

/*
 *	This routine copies from a user buffer into a socket,
 *	and starts the transmit system.
 *
 *	Note: must be called with the socket locked.
 */

int tcp_do_sendmsg(struct sock *sk, struct msghdr *msg)
{
	struct iovec *iov;
	struct tcp_opt *tp;
	struct sk_buff *skb;
	int iovlen, flags;
	int mss_now;
	int err, copied;

	lock_sock(sk);

	err = 0;
	tp = &(sk->tp_pinfo.af_tcp);

	/* Wait for a connection to finish. */
	flags = msg->msg_flags;
	if ((1 << sk->state) & ~(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT))
		if((err = wait_for_tcp_connect(sk, flags)) != 0)
			goto out;

	/* This should be in poll */
	sk->socket->flags &= ~SO_NOSPACE; /* clear SIGIO XXX */

	mss_now = tcp_current_mss(sk);

	/* Ok commence sending. */
	iovlen = msg->msg_iovlen;
	iov = msg->msg_iov;
	copied = 0;

	while(--iovlen >= 0) {
		int seglen=iov->iov_len;
		unsigned char * from=iov->iov_base;

		iov++;

		while(seglen > 0) {
			int copy, tmp, queue_it, psh;

			if (err)
				goto do_fault2;

			/* Stop on errors. */
			if (sk->err)
				goto do_sock_err;

			/* Make sure that we are established. */
			if (sk->shutdown & SEND_SHUTDOWN)
				goto do_shutdown;

			/* Now we need to check if we have a half
			 * built packet we can tack some data onto.
			 */
			if (tp->send_head && !(flags & MSG_OOB)) {
				skb = sk->write_queue.prev;
				copy = skb->len;
				/* If the remote does SWS avoidance we should
				 * queue the best we can if not we should in
				 * fact send multiple packets...
				 * A method for detecting this would be most
				 * welcome.
				 */
				if (skb_tailroom(skb) > 0 &&
				    (mss_now - copy) > 0 &&
				    tp->snd_nxt < TCP_SKB_CB(skb)->end_seq) {
					int last_byte_was_odd = (copy % 4);

					/*
					 * Check for parallel writers sleeping in user access.
					 */
					if (tp->partial_writers++ > 0) {
						wait_for_buffer(sk);
						tp->partial_writers--;
						continue;
					}

					copy = mss_now - copy;
					if(copy > skb_tailroom(skb))
						copy = skb_tailroom(skb);
					if(copy > seglen)
						copy = seglen;

					if(last_byte_was_odd) {
						if(copy_from_user(skb_put(skb, copy),
								  from, copy))
							err = -EFAULT;
						skb->csum = csum_partial(skb->data,
									 skb->len, 0);
					} else {
						skb->csum =
							csum_and_copy_from_user(
							from, skb_put(skb, copy),
							copy, skb->csum, &err);
					}

					/*
					 * FIXME: the *_user functions should
					 *	  return how much data was
					 *	  copied before the fault
					 *	  occurred and then a partial
					 *	  packet with this data should
					 *	  be sent.  Unfortunately
					 *	  csum_and_copy_from_user doesn't
					 *	  return this information.
					 *	  ATM it might send partly zeroed
					 *	  data in this case.
					 */
					tp->write_seq += copy;
					TCP_SKB_CB(skb)->end_seq += copy;
					from += copy;
					copied += copy;
					seglen -= copy;
					if (PSH_NEEDED)
						TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_PSH;

					if (--tp->partial_writers > 0)
						wake_up_interruptible(sk->sleep);

					continue;
				}
			}

			/* We also need to worry about the window.  If
			 * window < 1/2 the maximum window we've seen
			 * from this host, don't use it.  This is
			 * sender side silly window prevention, as
			 * specified in RFC1122.  (Note that this is
			 * different than earlier versions of SWS
			 * prevention, e.g. RFC813.).  What we
			 * actually do is use the whole MSS.  Since
			 * the results in the right edge of the packet
			 * being outside the window, it will be queued
			 * for later rather than sent.
			 */
			psh = 0;
			copy = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);
			if(copy > (tp->max_window >> 1)) {
				copy = min(copy, mss_now);
				psh = 1;
			} else {
				copy = mss_now;
			}
			if(copy > seglen)
				copy = seglen;

			/* Determine how large of a buffer to allocate.  */
			tmp = MAX_HEADER + sk->prot->max_header;
			if (copy < min(mss_now, tp->max_window >> 1) &&
			    !(flags & MSG_OOB)) {
				tmp += min(mss_now, tp->max_window);

				/* What is happening here is that we want to
				 * tack on later members of the users iovec
				 * if possible into a single frame.  When we
				 * leave this loop our caller checks to see if
				 * we can send queued frames onto the wire.
				 * See tcp_v[46]_sendmsg() for this.
				 */
				queue_it = 1;
			} else {
				tmp += copy;
				queue_it = 0;
			}
			skb = sock_wmalloc(sk, tmp, 0, GFP_KERNEL);

			/* If we didn't get any memory, we need to sleep. */
			if (skb == NULL) {
				sk->socket->flags |= SO_NOSPACE;
				if (flags&MSG_DONTWAIT) {
					err = -EAGAIN;
					goto do_interrupted;
				}
				if (signal_pending(current)) {
					err = -ERESTARTSYS;
					goto do_interrupted;
				}
				tcp_push_pending_frames(sk, tp);
				wait_for_tcp_memory(sk);

				/* If SACK's were formed or PMTU events happened,
				 * we must find out about it.
				 */
				mss_now = tcp_current_mss(sk);
				continue;
			}

			seglen -= copy;

			/* Prepare control bits for TCP header creation engine. */
			TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK |
						  ((PSH_NEEDED || psh) ?
						   TCPCB_FLAG_PSH : 0));
			TCP_SKB_CB(skb)->sacked = 0;
			if (flags & MSG_OOB) {
				TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_URG;
				TCP_SKB_CB(skb)->urg_ptr = copy;
			} else
				TCP_SKB_CB(skb)->urg_ptr = 0;

			/* TCP data bytes are SKB_PUT() on top, later
			 * TCP+IP+DEV headers are SKB_PUSH()'d beneath.
			 * Reserve header space and checksum the data.
			 */
			skb_reserve(skb, MAX_HEADER + sk->prot->max_header);
			skb->csum = csum_and_copy_from_user(from,
					skb_put(skb, copy), copy, 0, &err);

			if (err)
				goto do_fault;

			from += copy;
			copied += copy;

			TCP_SKB_CB(skb)->seq = tp->write_seq;
			TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + copy;

			/* This advances tp->write_seq for us. */
			tcp_send_skb(sk, skb, queue_it);
		}
	}
	sk->err = 0;
	err = copied;
	goto out;

do_sock_err:
	if(copied)
		err = copied;
	else
		err = sock_error(sk);
	goto out;
do_shutdown:
	if(copied)
		err = copied;
	else {
		if (!(flags&MSG_NOSIGNAL))
			send_sig(SIGPIPE, current, 0);
		err = -EPIPE;
	}
	goto out;
do_interrupted:
	if(copied)
		err = copied;
	goto out;
do_fault:
	kfree_skb(skb);
do_fault2:
	err = -EFAULT;
out:
	tcp_push_pending_frames(sk, tp);
	release_sock(sk);
	return err;
}

#undef PSH_NEEDED

/*
 *	Send an ack if one is backlogged at this point. Ought to merge
 *	this with tcp_send_ack().
 *      This is called for delayed acks also.
 */

void tcp_read_wakeup(struct sock *sk)
{
	/* If we're closed, don't send an ack, or we'll get a RST
	 * from the closed destination.
	 */
	if (sk->state != TCP_CLOSE)
		tcp_send_ack(sk);
}

/*
 *	Handle reading urgent data. BSD has very simple semantics for
 *	this, no blocking and very strange errors 8)
 */

static int tcp_recv_urg(struct sock * sk, int nonblock,
			struct msghdr *msg, int len, int flags,
			int *addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* No URG data to read. */
	if (sk->urginline || !tp->urg_data || tp->urg_data == URG_READ)
		return -EINVAL;	/* Yes this is right ! */

	if (sk->err)
		return sock_error(sk);

	if (sk->state == TCP_CLOSE && !sk->done)
		return -ENOTCONN;

	if (sk->state == TCP_CLOSE || (sk->shutdown & RCV_SHUTDOWN))
		return 0;

	lock_sock(sk);
	if (tp->urg_data & URG_VALID) {
		int err = 0;
		char c = tp->urg_data;

		if (!(flags & MSG_PEEK))
			tp->urg_data = URG_READ;

		if(msg->msg_name)
			tp->af_specific->addr2sockaddr(sk, (struct sockaddr *)
						       msg->msg_name);

		if(addr_len)
			*addr_len = tp->af_specific->sockaddr_len;

		/* Read urgent data. */
		msg->msg_flags|=MSG_OOB;
		release_sock(sk);

		if(len>0)
		{
			err = memcpy_toiovec(msg->msg_iov, &c, 1);
			/* N.B. already set above ... */
			msg->msg_flags|=MSG_OOB;
		}
		else
			msg->msg_flags|=MSG_TRUNC;

		/* N.B. Is this right?? If len == 0 we didn't read any data */
		return err ? -EFAULT : 1;
	}
	release_sock(sk);

	/* Fixed the recv(..., MSG_OOB) behaviour.  BSD docs and
	 * the available implementations agree in this case:
	 * this call should never block, independent of the
	 * blocking state of the socket.
	 * Mike <pall@rz.uni-karlsruhe.de>
	 */
	return -EAGAIN;
}

/*
 *	Release a skb if it is no longer needed. This routine
 *	must be called with interrupts disabled or with the
 *	socket locked so that the sk_buff queue operation is ok.
 */

static inline void tcp_eat_skb(struct sock *sk, struct sk_buff * skb)
{
	__skb_unlink(skb, &sk->receive_queue);
	kfree_skb(skb);
}

/* Clean up the receive buffer for full frames taken by the user,
 * then send an ACK if necessary.  COPIED is the number of bytes
 * tcp_recvmsg has given to the user so far, it speeds up the
 * calculation of whether or not we must ACK for the sake of
 * a window update.
 */
static void cleanup_rbuf(struct sock *sk, int copied)
{
	struct sk_buff *skb;

	/* NOTE! The socket must be locked, so that we don't get
	 * a messed-up receive queue.
	 */
	while ((skb=skb_peek(&sk->receive_queue)) != NULL) {
		if (!skb->used || atomic_read(&skb->users) > 1)
			break;
		tcp_eat_skb(sk, skb);
	}

  	/* We send an ACK if we can now advertise a non-zero window
	 * which has been raised "significantly".
  	 */
	if(copied > 0) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		__u32 rcv_window_now = tcp_receive_window(tp);
		__u32 new_window = __tcp_select_window(sk);

		/* We won't be raising the window any further than
		 * the window-clamp allows.  Our window selection
		 * also keeps things a nice multiple of MSS.  These
		 * checks are necessary to prevent spurious ACKs
		 * which don't advertize a larger window.
		 */
		if((new_window && (new_window >= rcv_window_now * 2)) &&
		   ((rcv_window_now + tp->mss_cache) <= tp->window_clamp))
			tcp_read_wakeup(sk);
	}
}


/*
 *	This routine copies from a sock struct into the user buffer.
 */

int tcp_recvmsg(struct sock *sk, struct msghdr *msg,
		int len, int nonblock, int flags, int *addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct wait_queue wait = { current, NULL };
	int copied = 0;
	u32 peek_seq;
	volatile u32 *seq;	/* So gcc doesn't overoptimise */
	unsigned long used;
	int err = 0;
	int target = 1;		/* Read at least this many bytes */

	if (sk->state == TCP_LISTEN)
		return -ENOTCONN;

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		return tcp_recv_urg(sk, nonblock, msg, len, flags, addr_len);

	/*	Copying sequence to update. This is volatile to handle
	 *	the multi-reader case neatly (memcpy_to/fromfs might be
	 *	inline and thus not flush cached variables otherwise).
	 */
	peek_seq = tp->copied_seq;
	seq = &tp->copied_seq;
	if (flags & MSG_PEEK)
		seq = &peek_seq;

	/* Handle the POSIX bogosity MSG_WAITALL. */
	if (flags & MSG_WAITALL)
		target=len;

	add_wait_queue(sk->sleep, &wait);
	lock_sock(sk);

	/*
	 *	BUG BUG BUG
	 *	This violates 1003.1g compliance. We must wait for
	 *	data to exist even if we read none!
	 */

	while (len > 0) {
		struct sk_buff * skb;
		u32 offset;

		/* Are we at urgent data? Stop if we have read anything. */
		if (copied && tp->urg_data && tp->urg_seq == *seq)
			break;

		/* We need to check signals first, to get correct SIGURG
		 * handling. FIXME: Need to check this doesn't impact 1003.1g
		 * and move it down to the bottom of the loop
		 */
		if (signal_pending(current)) {
			if (copied)
				break;
			copied = -ERESTARTSYS;
			if (nonblock)
				copied = -EAGAIN;
			break;
		}

		/* Next get a buffer. */
		current->state = TASK_INTERRUPTIBLE;

		skb = skb_peek(&sk->receive_queue);
		do {
			if (!skb)
				break;

			/* Now that we have two receive queues this
			 * shouldn't happen.
			 */
			if (before(*seq, TCP_SKB_CB(skb)->seq)) {
				printk(KERN_INFO "recvmsg bug: copied %X seq %X\n",
				       *seq, TCP_SKB_CB(skb)->seq);
				break;
			}
			offset = *seq - TCP_SKB_CB(skb)->seq;
			if (skb->h.th->syn)
				offset--;
			if (offset < skb->len)
				goto found_ok_skb;
			if (skb->h.th->fin)
				goto found_fin_ok;
			if (!(flags & MSG_PEEK))
				skb->used = 1;
			skb = skb->next;
		} while (skb != (struct sk_buff *)&sk->receive_queue);

		if (copied >= target)
			break;

		if (copied) {
			if (sk->err ||
			    sk->state == TCP_CLOSE ||
			    (sk->shutdown & RCV_SHUTDOWN) ||
			    nonblock)
				break;
		} else {
			if (sk->done)
				break;

			if (sk->err) {
				copied = sock_error(sk);
				break;
			}

			if (sk->shutdown & RCV_SHUTDOWN)
				break;

			if (sk->state == TCP_CLOSE) {
				if (!sk->done) {
					copied = -ENOTCONN;
					break;
				}
				break;
			}

			if (nonblock) {
				copied = -EAGAIN;
				break;
			}
		}

		cleanup_rbuf(sk, copied);
		release_sock(sk);
		sk->socket->flags |= SO_WAITDATA;
		schedule();
		sk->socket->flags &= ~SO_WAITDATA;
		lock_sock(sk);
		continue;

	found_ok_skb:
		/*	Lock the buffer. We can be fairly relaxed as
		 *	an interrupt will never steal a buffer we are
		 *	using unless I've missed something serious in
		 *	tcp_data.
		 */
		atomic_inc(&skb->users);

		/* Ok so how much can we use? */
		used = skb->len - offset;
		if (len < used)
			used = len;

		/* Do we have urgent data here? */
		if (tp->urg_data) {
			u32 urg_offset = tp->urg_seq - *seq;
			if (urg_offset < used) {
				if (!urg_offset) {
					if (!sk->urginline) {
						++*seq;
						offset++;
						used--;
					}
				} else
					used = urg_offset;
			}
		}

		/*	Copy it - We _MUST_ update *seq first so that we
		 *	don't ever double read when we have dual readers
		 */
		*seq += used;

		/*	This memcpy_toiovec can sleep. If it sleeps and we
		 *	do a second read it relies on the skb->users to avoid
		 *	a crash when cleanup_rbuf() gets called.
		 */
		err = memcpy_toiovec(msg->msg_iov, ((unsigned char *)skb->h.th) + skb->h.th->doff*4 + offset, used);
		if (err) {
			/* Exception. Bailout! */
			atomic_dec(&skb->users);
			copied = -EFAULT;
			break;
		}

		copied += used;
		len -= used;

		/*	We now will not sleep again until we are finished
		 *	with skb. Sorry if you are doing the SMP port
		 *	but you'll just have to fix it neatly ;)
		 */
		atomic_dec(&skb->users);

		if (after(tp->copied_seq,tp->urg_seq))
			tp->urg_data = 0;
		if (used + offset < skb->len)
			continue;

		/*	Process the FIN. We may also need to handle PSH
		 *	here and make it break out of MSG_WAITALL.
		 */
		if (skb->h.th->fin)
			goto found_fin_ok;
		if (flags & MSG_PEEK)
			continue;
		skb->used = 1;
		if (atomic_read(&skb->users) == 1)
			tcp_eat_skb(sk, skb);
		continue;

	found_fin_ok:
		++*seq;
		if (flags & MSG_PEEK)
			break;

		/* All is done. */
		skb->used = 1;
		sk->shutdown |= RCV_SHUTDOWN;
		break;
	}

	if(copied >= 0 && msg->msg_name) {
		tp->af_specific->addr2sockaddr(sk, (struct sockaddr *)
					       msg->msg_name);
		if(addr_len)
			*addr_len = tp->af_specific->sockaddr_len;
	}

	remove_wait_queue(sk->sleep, &wait);
	current->state = TASK_RUNNING;

	/* Clean up data we have read: This will do ACK frames. */
	cleanup_rbuf(sk, copied);
	release_sock(sk);
	return copied;
}

/*
 * Check whether to renew the timer.
 */
static inline void tcp_check_fin_timer(struct sock *sk)
{
	if (sk->state == TCP_FIN_WAIT2 && !sk->timer.prev)
		tcp_reset_msl_timer(sk, TIME_CLOSE, sysctl_tcp_fin_timeout);
}

/*
 *	State processing on a close. This implements the state shift for
 *	sending our FIN frame. Note that we only send a FIN for some
 *	states. A shutdown() may have already sent the FIN, or we may be
 *	closed.
 */

static unsigned char new_state[16] = {
  /* current state:        new state:      action:	*/
  /* (Invalid)		*/ TCP_CLOSE,
  /* TCP_ESTABLISHED	*/ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
  /* TCP_SYN_SENT	*/ TCP_CLOSE,
  /* TCP_SYN_RECV	*/ TCP_FIN_WAIT1 | TCP_ACTION_FIN,
  /* TCP_FIN_WAIT1	*/ TCP_FIN_WAIT1,
  /* TCP_FIN_WAIT2	*/ TCP_FIN_WAIT2,
  /* TCP_TIME_WAIT	*/ TCP_CLOSE,
  /* TCP_CLOSE		*/ TCP_CLOSE,
  /* TCP_CLOSE_WAIT	*/ TCP_LAST_ACK  | TCP_ACTION_FIN,
  /* TCP_LAST_ACK	*/ TCP_LAST_ACK,
  /* TCP_LISTEN		*/ TCP_CLOSE,
  /* TCP_CLOSING	*/ TCP_CLOSING,
};

static int tcp_close_state(struct sock *sk, int dead)
{
	int next = (int) new_state[sk->state];
	int ns = (next & TCP_STATE_MASK);

	tcp_set_state(sk, ns);

	/*	This is a (useful) BSD violating of the RFC. There is a
	 *	problem with TCP as specified in that the other end could
	 *	keep a socket open forever with no application left this end.
	 *	We use a 3 minute timeout (about the same as BSD) then kill
	 *	our end. If they send after that then tough - BUT: long enough
	 *	that we won't make the old 4*rto = almost no time - whoops
	 *	reset mistake.
	 */
	if (dead)
		tcp_check_fin_timer(sk);

	return (next & TCP_ACTION_FIN);
}

/*
 *	Shutdown the sending side of a connection. Much like close except
 *	that we don't receive shut down or set sk->dead.
 */

void tcp_shutdown(struct sock *sk, int how)
{
	/*	We need to grab some memory, and put together a FIN,
	 *	and then put it into the queue to be sent.
	 *		Tim MacKenzie(tym@dibbler.cs.monash.edu.au) 4 Dec '92.
	 */
	if (!(how & SEND_SHUTDOWN))
		return;

	/* If we've already sent a FIN, or it's a closed state, skip this. */
	if ((1 << sk->state) &
	    (TCPF_ESTABLISHED|TCPF_SYN_SENT|TCPF_SYN_RECV|TCPF_CLOSE_WAIT)) {
		lock_sock(sk);

		/* Clear out any half completed packets.  FIN if needed. */
		if (tcp_close_state(sk,0))
			tcp_send_fin(sk);

		release_sock(sk);
	}
}


/*
 *	Return 1 if we still have things to send in our buffers.
 */

static inline int closing(struct sock * sk)
{
	return ((1 << sk->state) & (TCPF_FIN_WAIT1|TCPF_CLOSING|TCPF_LAST_ACK));
}

/*
 *	This routine closes sockets which have been at least partially
 *	opened, but not yet accepted. Currently it is only called by
 *	tcp_close, and timeout mirrors the value there.
 */

static void tcp_close_pending (struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct open_request *req = tp->syn_wait_queue;

	while(req) {
		struct open_request *iter;

		if (req->sk)
			tcp_close(req->sk, 0);

		iter = req;
		req = req->dl_next;

		(*iter->class->destructor)(iter);
		tcp_dec_slow_timer(TCP_SLT_SYNACK);
		sk->ack_backlog--;
		tcp_openreq_free(iter);
	}

	tcp_synq_init(tp);
}

void tcp_close(struct sock *sk, long timeout)
{
	struct sk_buff *skb;
	int data_was_unread = 0;

	/*
	 * Check whether the socket is locked ... supposedly
	 * it's impossible to tcp_close() a locked socket.
	 */
	if (atomic_read(&sk->sock_readers))
		printk("tcp_close: socket already locked!\n");

	/* We need to grab some memory, and put together a FIN,
	 * and then put it into the queue to be sent.
	 */
	lock_sock(sk);
	if(sk->state == TCP_LISTEN) {
		/* Special case. */
		tcp_set_state(sk, TCP_CLOSE);
		tcp_close_pending(sk);
		release_sock(sk);
		sk->dead = 1;
		return;
	}

	/* It is questionable, what the role of this is now.
	 * In any event either it should be removed, or
	 * increment of SLT_KEEPALIVE be done, this is causing
	 * big problems.  For now I comment it out.  -DaveM
	 */
	/* sk->keepopen = 1; */
	sk->shutdown = SHUTDOWN_MASK;

	if (!sk->dead)
	  	sk->state_change(sk);

	/*  We need to flush the recv. buffs.  We do this only on the
	 *  descriptor close, not protocol-sourced closes, because the
	 *  reader process may not have drained the data yet!
	 */
	while((skb=__skb_dequeue(&sk->receive_queue))!=NULL) {
		u32 len = TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq - skb->h.th->fin;
		data_was_unread += len;
		kfree_skb(skb);
	}

	/* As outlined in draft-ietf-tcpimpl-prob-03.txt, section
	 * 3.10, we send a RST here because data was lost.  To
	 * witness the awful effects of the old behavior of always
	 * doing a FIN, run an older 2.1.x kernel or 2.0.x, start
	 * a bulk GET in an FTP client, suspend the process, wait
	 * for the client to advertise a zero window, then kill -9
	 * the FTP client, wheee...  Note: timeout is always zero
	 * in such a case.
	 */
	if(data_was_unread != 0) {
		/* Unread data was tossed, zap the connection. */
		tcp_set_state(sk, TCP_CLOSE);
		tcp_send_active_reset(sk);
	} else if (tcp_close_state(sk,1)) {
		/* We FIN if the application ate all the data before
		 * zapping the connection.
		 */
		tcp_send_fin(sk);
	}

	if (timeout) {
		struct task_struct *tsk = current;
		struct wait_queue wait = { tsk, NULL };

		add_wait_queue(sk->sleep, &wait);
		release_sock(sk);

		while (1) {
			tsk->state = TASK_INTERRUPTIBLE;
			if (!closing(sk))
				break;
			timeout = schedule_timeout(timeout);
			if (signal_pending(tsk) || !timeout)
				break;
		}

		tsk->state = TASK_RUNNING;
		remove_wait_queue(sk->sleep, &wait);

		lock_sock(sk);
	}

	/* Now that the socket is dead, if we are in the FIN_WAIT2 state
	 * we may need to set up a timer.
         */
	tcp_check_fin_timer(sk);

	release_sock(sk);
	sk->dead = 1;
}

/*
 *	Wait for an incoming connection, avoid race
 *	conditions. This must be called with the socket locked.
 */
static struct open_request * wait_for_connect(struct sock * sk,
					      struct open_request **pprev)
{
	struct wait_queue wait = { current, NULL };
	struct open_request *req;

	add_wait_queue(sk->sleep, &wait);
	for (;;) {
		current->state = TASK_INTERRUPTIBLE;
		release_sock(sk);
		schedule();
		lock_sock(sk);
		req = tcp_find_established(&(sk->tp_pinfo.af_tcp), pprev);
		if (req)
			break;
		if (signal_pending(current))
			break;
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);
	return req;
}

/*
 *	This will accept the next outstanding connection.
 *
 *	Be careful about race conditions here - this is subtle.
 */

struct sock *tcp_accept(struct sock *sk, int flags)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	struct open_request *req, *prev;
	struct sock *newsk = NULL;
	int error;

	lock_sock(sk);

	/* We need to make sure that this socket is listening,
	 * and that it has something pending.
	 */
	error = EINVAL;
	if (sk->state != TCP_LISTEN)
		goto out;

	/* Find already established connection */
	req = tcp_find_established(tp, &prev);
	if (!req) {
		/* If this is a non blocking socket don't sleep */
		error = EAGAIN;
		if (flags & O_NONBLOCK)
			goto out;

		error = ERESTARTSYS;
		req = wait_for_connect(sk, &prev);
		if (!req)
			goto out;
	}

	tcp_synq_unlink(tp, req, prev);
	newsk = req->sk;
	req->class->destructor(req);
	tcp_openreq_free(req);
	sk->ack_backlog--;
	if(sk->keepopen)
		tcp_inc_slow_timer(TCP_SLT_KEEPALIVE);

	release_sock(sk);
	return newsk;

out:
	/* sk should be in LISTEN state, thus accept can use sk->err for
	 * internal purposes without stomping one anyone's feed.
	 */
	sk->err = error;
	release_sock(sk);
	return newsk;
}

/*
 *	Socket option code for TCP.
 */

int tcp_setsockopt(struct sock *sk, int level, int optname, char *optval,
		   int optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val;

	if (level != SOL_TCP)
		return tp->af_specific->setsockopt(sk, level, optname,
						   optval, optlen);

	if(optlen<sizeof(int))
		return -EINVAL;

  	if (get_user(val, (int *)optval))
		return -EFAULT;

	switch(optname) {
	case TCP_MAXSEG:
		/* values greater than interface MTU won't take effect.  however at
		 * the point when this call is done we typically don't yet know
		 * which interface is going to be used
		 */
		if(val < 1 || val > MAX_WINDOW)
			return -EINVAL;
		tp->user_mss = val;
		return 0;

	case TCP_NODELAY:
		/* You cannot try to use this and TCP_CORK in
		 * tandem, so let the user know.
		 */
		if (sk->nonagle == 2)
			return -EINVAL;
		sk->nonagle = (val == 0) ? 0 : 1;
		return 0;

	case TCP_CORK:
		/* When set indicates to always queue non-full frames.
		 * Later the user clears this option and we transmit
		 * any pending partial frames in the queue.  This is
		 * meant to be used alongside sendfile() to get properly
		 * filled frames when the user (for example) must write
		 * out headers with a write() call first and then use
		 * sendfile to send out the data parts.
		 *
		 * You cannot try to use TCP_NODELAY and this mechanism
		 * at the same time, so let the user know.
		 */
		if (sk->nonagle == 1)
			return -EINVAL;
		if (val != 0) {
			sk->nonagle = 2;
		} else {
			sk->nonagle = 0;

			lock_sock(sk);
			tcp_push_pending_frames(sk, tp);
			release_sock(sk);
		}
		return 0;

	default:
		return -ENOPROTOOPT;
	};
}

int tcp_getsockopt(struct sock *sk, int level, int optname, char *optval,
		   int *optlen)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int val, len;

	if(level != SOL_TCP)
		return tp->af_specific->getsockopt(sk, level, optname,
						   optval, optlen);

	if(get_user(len,optlen))
		return -EFAULT;

	len = min(len, sizeof(int));

	switch(optname) {
	case TCP_MAXSEG:
		val = tp->user_mss;
		break;
	case TCP_NODELAY:
		val = (sk->nonagle == 1);
		break;
	case TCP_CORK:
		val = (sk->nonagle == 2);
		break;
	default:
		return -ENOPROTOOPT;
	};

  	if(put_user(len, optlen))
  		return -EFAULT;
	if(copy_to_user(optval, &val,len))
		return -EFAULT;
  	return 0;
}

void tcp_set_keepalive(struct sock *sk, int val)
{
	if (!sk->keepopen && val)
		tcp_inc_slow_timer(TCP_SLT_KEEPALIVE);
	else if (sk->keepopen && !val)
		tcp_dec_slow_timer(TCP_SLT_KEEPALIVE);
}

extern void __skb_cb_too_small_for_tcp(int, int);

void __init tcp_init(void)
{
	struct sk_buff *skb = NULL;
	unsigned long goal;
	int order;

	if(sizeof(struct tcp_skb_cb) > sizeof(skb->cb))
		__skb_cb_too_small_for_tcp(sizeof(struct tcp_skb_cb),
					   sizeof(skb->cb));

	tcp_openreq_cachep = kmem_cache_create("tcp_open_request",
						   sizeof(struct open_request),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
	if(!tcp_openreq_cachep)
		panic("tcp_init: Cannot alloc open_request cache.");

	tcp_bucket_cachep = kmem_cache_create("tcp_bind_bucket",
					      sizeof(struct tcp_bind_bucket),
					      0, SLAB_HWCACHE_ALIGN,
					      NULL, NULL);
	if(!tcp_bucket_cachep)
		panic("tcp_init: Cannot alloc tcp_bind_bucket cache.");

	tcp_timewait_cachep = kmem_cache_create("tcp_tw_bucket",
						sizeof(struct tcp_tw_bucket),
						0, SLAB_HWCACHE_ALIGN,
						NULL, NULL);
	if(!tcp_timewait_cachep)
		panic("tcp_init: Cannot alloc tcp_tw_bucket cache.");

	/* Size and allocate TCP hash tables. */
	goal = num_physpages >> (20 - PAGE_SHIFT);
	for (order = 0; (1UL << order) < goal; order++)
		;
	do {
		tcp_ehash_size = (1UL << order) * PAGE_SIZE /
			sizeof(struct sock *);
		tcp_ehash = (struct sock **)
			__get_free_pages(GFP_ATOMIC, order);
	} while (tcp_ehash == NULL && --order >= 0);

	if (!tcp_ehash)
		panic("Failed to allocate TCP established hash table\n");
	memset(tcp_ehash, 0, tcp_ehash_size * sizeof(struct sock *));

	goal = (((1UL << order) * PAGE_SIZE) / sizeof(struct tcp_bind_bucket *));
	if (goal > (64 * 1024)) {
		/* Don't size the bind-hash larger than the port
		 * space, that is just silly.
		 */
		goal = (((64 * 1024) * sizeof(struct tcp_bind_bucket *)) / PAGE_SIZE);
		for (order = 0; (1UL << order) < goal; order++)
			;
	}

	do {
		tcp_bhash_size = (1UL << order) * PAGE_SIZE /
			sizeof(struct tcp_bind_bucket *);
		tcp_bhash = (struct tcp_bind_bucket **)
			__get_free_pages(GFP_ATOMIC, order);
	} while (tcp_bhash == NULL && --order >= 0);

	if (!tcp_bhash)
		panic("Failed to allocate TCP bind hash table\n");
	memset(tcp_bhash, 0, tcp_bhash_size * sizeof(struct tcp_bind_bucket *));

	printk("TCP: Hash tables configured (ehash %d bhash %d)\n",
	       tcp_ehash_size, tcp_bhash_size);
}
