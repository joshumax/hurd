/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_output.c,v 1.108.2.1 1999/05/14 23:07:36 davem Exp $
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
 */

/*
 * Changes:	Pedro Roque	:	Retransmit queue handled by TCP.
 *				:	Fragmentation on mtu decrease
 *				:	Segment collapse on retransmit
 *				:	AF independence
 *
 *		Linus Torvalds	:	send_delayed_ack
 *		David S. Miller	:	Charge memory using the right skb
 *					during syn/ack processing.
 *		David S. Miller :	Output engine completely rewritten.
 *		Andrea Arcangeli:	SYNACK carry ts_recent in tsecr.
 *
 */

#include <net/tcp.h>

extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_sack;

/* People can turn this off for buggy TCP's found in printers etc. */
int sysctl_tcp_retrans_collapse = 1;

/* Get rid of any delayed acks, we sent one already.. */
static __inline__ void clear_delayed_acks(struct sock * sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tp->delayed_acks = 0;
	if(tcp_in_quickack_mode(tp))
		tcp_exit_quickack_mode(tp);
	tcp_clear_xmit_timer(sk, TIME_DACK);
}

static __inline__ void update_send_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	tp->send_head = tp->send_head->next;
	if (tp->send_head == (struct sk_buff *) &sk->write_queue)
		tp->send_head = NULL;
}

/* This routine actually transmits TCP packets queued in by
 * tcp_do_sendmsg().  This is used by both the initial
 * transmission and possible later retransmissions.
 * All SKB's seen here are completely headerless.  It is our
 * job to build the TCP header, and pass the packet down to
 * IP so it can do the same plus pass the packet off to the
 * device.
 *
 * We are working here with either a clone of the original
 * SKB, or a fresh unique copy made by the retransmit engine.
 */
void tcp_transmit_skb(struct sock *sk, struct sk_buff *skb)
{
	if(skb != NULL) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);
		int tcp_header_size = tp->tcp_header_len;
		struct tcphdr *th;
		int sysctl_flags;

#define SYSCTL_FLAG_TSTAMPS	0x1
#define SYSCTL_FLAG_WSCALE	0x2
#define SYSCTL_FLAG_SACK	0x4

		sysctl_flags = 0;
		if(tcb->flags & TCPCB_FLAG_SYN) {
			tcp_header_size = sizeof(struct tcphdr) + TCPOLEN_MSS;
			if(sysctl_tcp_timestamps) {
				tcp_header_size += TCPOLEN_TSTAMP_ALIGNED;
				sysctl_flags |= SYSCTL_FLAG_TSTAMPS;
			}
			if(sysctl_tcp_window_scaling) {
				tcp_header_size += TCPOLEN_WSCALE_ALIGNED;
				sysctl_flags |= SYSCTL_FLAG_WSCALE;
			}
			if(sysctl_tcp_sack) {
				sysctl_flags |= SYSCTL_FLAG_SACK;
				if(!(sysctl_flags & SYSCTL_FLAG_TSTAMPS))
					tcp_header_size += TCPOLEN_SACKPERM_ALIGNED;
			}
		} else if(tp->sack_ok && tp->num_sacks) {
			/* A SACK is 2 pad bytes, a 2 byte header, plus
			 * 2 32-bit sequence numbers for each SACK block.
			 */
			tcp_header_size += (TCPOLEN_SACK_BASE_ALIGNED +
					    (tp->num_sacks * TCPOLEN_SACK_PERBLOCK));
		}
		th = (struct tcphdr *) skb_push(skb, tcp_header_size);
		skb->h.th = th;
		skb_set_owner_w(skb, sk);

		/* Build TCP header and checksum it. */
		th->source		= sk->sport;
		th->dest		= sk->dport;
		th->seq			= htonl(TCP_SKB_CB(skb)->seq);
		th->ack_seq		= htonl(tp->rcv_nxt);
		th->doff		= (tcp_header_size >> 2);
		th->res1		= 0;
		*(((__u8 *)th) + 13)	= tcb->flags;
		if(!(tcb->flags & TCPCB_FLAG_SYN))
			th->window	= htons(tcp_select_window(sk));
		th->check		= 0;
		th->urg_ptr		= ntohs(tcb->urg_ptr);
		if(tcb->flags & TCPCB_FLAG_SYN) {
			/* RFC1323: The window in SYN & SYN/ACK segments
			 * is never scaled.
			 */
			th->window	= htons(tp->rcv_wnd);
			tcp_syn_build_options((__u32 *)(th + 1), tp->mss_clamp,
					      (sysctl_flags & SYSCTL_FLAG_TSTAMPS),
					      (sysctl_flags & SYSCTL_FLAG_SACK),
					      (sysctl_flags & SYSCTL_FLAG_WSCALE),
					      tp->rcv_wscale,
					      TCP_SKB_CB(skb)->when,
		      			      tp->ts_recent);
		} else {
			tcp_build_and_update_options((__u32 *)(th + 1),
						     tp, TCP_SKB_CB(skb)->when);
		}
		tp->af_specific->send_check(sk, th, skb->len, skb);

		clear_delayed_acks(sk);
		tp->last_ack_sent = tp->rcv_nxt;
		tcp_statistics.TcpOutSegs++;
		tp->af_specific->queue_xmit(skb);
	}
#undef SYSCTL_FLAG_TSTAMPS
#undef SYSCTL_FLAG_WSCALE
#undef SYSCTL_FLAG_SACK
}

/* This is the main buffer sending routine. We queue the buffer
 * and decide whether to queue or transmit now.
 */
void tcp_send_skb(struct sock *sk, struct sk_buff *skb, int force_queue)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Advance write_seq and place onto the write_queue. */
	tp->write_seq += (TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq);
	__skb_queue_tail(&sk->write_queue, skb);

	if (!force_queue && tp->send_head == NULL && tcp_snd_test(sk, skb)) {
		/* Send it out now. */
		TCP_SKB_CB(skb)->when = tcp_time_stamp;
		tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
		tp->packets_out++;
		tcp_transmit_skb(sk, skb_clone(skb, GFP_KERNEL));
		if(!tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	} else {
		/* Queue it, remembering where we must start sending. */
		if (tp->send_head == NULL)
			tp->send_head = skb;
		if (!force_queue && tp->packets_out == 0 && !tp->pending) {
			tp->pending = TIME_PROBE0;
			tcp_reset_xmit_timer(sk, TIME_PROBE0, tp->rto);
		}
	}
}

/* Function to create two new TCP segments.  Shrinks the given segment
 * to the specified size and appends a new segment with the rest of the
 * packet to the list.  This won't be called frequently, I hope. 
 * Remember, these are still headerless SKBs at this point.
 */
static int tcp_fragment(struct sock *sk, struct sk_buff *skb, u32 len)
{
	struct sk_buff *buff;
	int nsize = skb->len - len;
	u16 flags;

	/* Get a new skb... force flag on. */
	buff = sock_wmalloc(sk,
			    (nsize + MAX_HEADER + sk->prot->max_header),
			    1, GFP_ATOMIC);
	if (buff == NULL)
		return -1; /* We'll just try again later. */

	/* Reserve space for headers. */
	skb_reserve(buff, MAX_HEADER + sk->prot->max_header);
		
	/* Correct the sequence numbers. */
	TCP_SKB_CB(buff)->seq = TCP_SKB_CB(skb)->seq + len;
	TCP_SKB_CB(buff)->end_seq = TCP_SKB_CB(skb)->end_seq;
	
	/* PSH and FIN should only be set in the second packet. */
	flags = TCP_SKB_CB(skb)->flags;
	TCP_SKB_CB(skb)->flags = flags & ~(TCPCB_FLAG_FIN | TCPCB_FLAG_PSH);
	if(flags & TCPCB_FLAG_URG) {
		u16 old_urg_ptr = TCP_SKB_CB(skb)->urg_ptr;

		/* Urgent data is always a pain in the ass. */
		if(old_urg_ptr > len) {
			TCP_SKB_CB(skb)->flags &= ~(TCPCB_FLAG_URG);
			TCP_SKB_CB(skb)->urg_ptr = 0;
			TCP_SKB_CB(buff)->urg_ptr = old_urg_ptr - len;
		} else {
			flags &= ~(TCPCB_FLAG_URG);
		}
	}
	if(!(flags & TCPCB_FLAG_URG))
		TCP_SKB_CB(buff)->urg_ptr = 0;
	TCP_SKB_CB(buff)->flags = flags;
	TCP_SKB_CB(buff)->sacked = 0;

	/* Copy and checksum data tail into the new buffer. */
	buff->csum = csum_partial_copy(skb->data + len, skb_put(buff, nsize),
				       nsize, 0);

	/* This takes care of the FIN sequence number too. */
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(buff)->seq;
	skb_trim(skb, len);

	/* Rechecksum original buffer. */
	skb->csum = csum_partial(skb->data, skb->len, 0);

	/* Looks stupid, but our code really uses when of
	 * skbs, which it never sent before. --ANK
	 */
	TCP_SKB_CB(buff)->when = TCP_SKB_CB(skb)->when;

	/* Link BUFF into the send queue. */
	__skb_append(skb, buff);

	return 0;
}

/* This function synchronize snd mss to current pmtu/exthdr set.

   tp->user_mss is mss set by user by TCP_MAXSEG. It does NOT counts
   for TCP options, but includes only bare TCP header.

   tp->mss_clamp is mss negotiated at connection setup.
   It is minimum of user_mss and mss received with SYN.
   It also does not include TCP options.

   tp->pmtu_cookie is last pmtu, seen by this function.

   tp->mss_cache is current effective sending mss, including
   all tcp options except for SACKs. It is evaluated,
   taking into account current pmtu, but never exceeds
   tp->mss_clamp.

   NOTE1. rfc1122 clearly states that advertised MSS
   DOES NOT include either tcp or ip options.

   NOTE2. tp->pmtu_cookie and tp->mss_cache are READ ONLY outside
   this function.			--ANK (980731)
 */

int tcp_sync_mss(struct sock *sk, u32 pmtu)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	int mss_now;

	/* Calculate base mss without TCP options:
	   It is MMS_S - sizeof(tcphdr) of rfc1122
	*/
	mss_now = pmtu - tp->af_specific->net_header_len - sizeof(struct tcphdr);

	/* Clamp it (mss_clamp does not include tcp options) */
	if (mss_now > tp->mss_clamp)
		mss_now = tp->mss_clamp;

	/* Now subtract TCP options size, not including SACKs */
	mss_now -= tp->tcp_header_len - sizeof(struct tcphdr);

	/* Now subtract optional transport overhead */
	mss_now -= tp->ext_header_len;

	/* It we got too small (or even negative) value,
	   clamp it by 8 from below. Why 8 ?
	   Well, it could be 1 with the same success,
	   but if IP accepted segment of length 1,
	   it would love 8 even more 8)		--ANK (980731)
	 */
	if (mss_now < 8)
		mss_now = 8;

	/* And store cached results */
	tp->pmtu_cookie = pmtu;
	tp->mss_cache = mss_now;
	return mss_now;
}


/* This routine writes packets to the network.  It advances the
 * send_head.  This happens as incoming acks open up the remote
 * window for us.
 */
void tcp_write_xmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	unsigned int mss_now;

	/* Account for SACKS, we may need to fragment due to this.
	 * It is just like the real MSS changing on us midstream.
	 * We also handle things correctly when the user adds some
	 * IP options mid-stream.  Silly to do, but cover it.
	 */
	mss_now = tcp_current_mss(sk); 

	/* If we are zapped, the bytes will have to remain here.
	 * In time closedown will empty the write queue and all
	 * will be happy.
	 */
	if(!sk->zapped) {
		struct sk_buff *skb;
		int sent_pkts = 0;

		/* Anything on the transmit queue that fits the window can
		 * be added providing we are:
		 *
		 * a) following SWS avoidance [and Nagle algorithm]
		 * b) not exceeding our congestion window.
		 * c) not retransmitting [Nagle]
		 */
		while((skb = tp->send_head) && tcp_snd_test(sk, skb)) {
			if (skb->len > mss_now) {
				if (tcp_fragment(sk, skb, mss_now))
					break;
			}

			/* Advance the send_head.  This one is going out. */
			update_send_head(sk);
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
			tp->packets_out++;
			tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
			sent_pkts = 1;
		}

		/* If we sent anything, make sure the retransmit
		 * timer is active.
		 */
		if (sent_pkts && !tcp_timer_is_set(sk, TIME_RETRANS))
			tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	}
}

/* This function returns the amount that we can raise the
 * usable window based on the following constraints
 *  
 * 1. The window can never be shrunk once it is offered (RFC 793)
 * 2. We limit memory per socket
 *
 * RFC 1122:
 * "the suggested [SWS] avoidance algorithm for the receiver is to keep
 *  RECV.NEXT + RCV.WIN fixed until:
 *  RCV.BUFF - RCV.USER - RCV.WINDOW >= min(1/2 RCV.BUFF, MSS)"
 *
 * i.e. don't raise the right edge of the window until you can raise
 * it at least MSS bytes.
 *
 * Unfortunately, the recommended algorithm breaks header prediction,
 * since header prediction assumes th->window stays fixed.
 *
 * Strictly speaking, keeping th->window fixed violates the receiver
 * side SWS prevention criteria. The problem is that under this rule
 * a stream of single byte packets will cause the right side of the
 * window to always advance by a single byte.
 * 
 * Of course, if the sender implements sender side SWS prevention
 * then this will not be a problem.
 * 
 * BSD seems to make the following compromise:
 * 
 *	If the free space is less than the 1/4 of the maximum
 *	space available and the free space is less than 1/2 mss,
 *	then set the window to 0.
 *	Otherwise, just prevent the window from shrinking
 *	and from being larger than the largest representable value.
 *
 * This prevents incremental opening of the window in the regime
 * where TCP is limited by the speed of the reader side taking
 * data out of the TCP receive queue. It does nothing about
 * those cases where the window is constrained on the sender side
 * because the pipeline is full.
 *
 * BSD also seems to "accidentally" limit itself to windows that are a
 * multiple of MSS, at least until the free space gets quite small.
 * This would appear to be a side effect of the mbuf implementation.
 * Combining these two algorithms results in the observed behavior
 * of having a fixed window size at almost all times.
 *
 * Below we obtain similar behavior by forcing the offered window to
 * a multiple of the mss when it is feasible to do so.
 *
 * Note, we don't "adjust" for TIMESTAMP or SACK option bytes.
 */
u32 __tcp_select_window(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	unsigned int mss = tp->mss_cache;
	int free_space;
	u32 window;

	/* Sometimes free_space can be < 0. */
	free_space = (sk->rcvbuf - atomic_read(&sk->rmem_alloc)) / 2;
	if (tp->window_clamp) {
		if (free_space > ((int) tp->window_clamp))
			free_space = tp->window_clamp;
		mss = min(tp->window_clamp, mss);
	} else {
		printk("tcp_select_window: tp->window_clamp == 0.\n");
	}

	if (mss < 1) {
		mss = 1;
		printk("tcp_select_window: sk->mss fell to 0.\n");
	}
	
	if ((free_space < (sk->rcvbuf/4)) && (free_space < ((int) (mss/2)))) {
		window = 0;
		tp->pred_flags = 0; 
	} else {
		/* Get the largest window that is a nice multiple of mss.
		 * Window clamp already applied above.
		 * If our current window offering is within 1 mss of the
		 * free space we just keep it. This prevents the divide
		 * and multiply from happening most of the time.
		 * We also don't do any window rounding when the free space
		 * is too small.
		 */
		window = tp->rcv_wnd;
		if ((((int) window) <= (free_space - ((int) mss))) ||
				(((int) window) > free_space))
			window = (((unsigned int) free_space)/mss)*mss;
	}
	return window;
}

/* Attempt to collapse two adjacent SKB's during retransmission. */
static void tcp_retrans_try_collapse(struct sock *sk, struct sk_buff *skb, int mss_now)
{
	struct sk_buff *next_skb = skb->next;

	/* The first test we must make is that neither of these two
	 * SKB's are still referenced by someone else.
	 */
	if(!skb_cloned(skb) && !skb_cloned(next_skb)) {
		int skb_size = skb->len, next_skb_size = next_skb->len;
		u16 flags = TCP_SKB_CB(skb)->flags;

		/* Punt if the first SKB has URG set. */
		if(flags & TCPCB_FLAG_URG)
			return;
	
		/* Also punt if next skb has been SACK'd. */
		if(TCP_SKB_CB(next_skb)->sacked & TCPCB_SACKED_ACKED)
			return;

		/* Punt if not enough space exists in the first SKB for
		 * the data in the second, or the total combined payload
		 * would exceed the MSS.
		 */
		if ((next_skb_size > skb_tailroom(skb)) ||
		    ((skb_size + next_skb_size) > mss_now))
			return;

		/* Ok.  We will be able to collapse the packet. */
		__skb_unlink(next_skb, next_skb->list);

		if(skb->len % 4) {
			/* Must copy and rechecksum all data. */
			memcpy(skb_put(skb, next_skb_size), next_skb->data, next_skb_size);
			skb->csum = csum_partial(skb->data, skb->len, 0);
		} else {
			/* Optimize, actually we could also combine next_skb->csum
			 * to skb->csum using a single add w/carry operation too.
			 */
			skb->csum = csum_partial_copy(next_skb->data,
						      skb_put(skb, next_skb_size),
						      next_skb_size, skb->csum);
		}
	
		/* Update sequence range on original skb. */
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(next_skb)->end_seq;

		/* Merge over control information. */
		flags |= TCP_SKB_CB(next_skb)->flags; /* This moves PSH/FIN etc. over */
		if(flags & TCPCB_FLAG_URG) {
			u16 urgptr = TCP_SKB_CB(next_skb)->urg_ptr;
			TCP_SKB_CB(skb)->urg_ptr = urgptr + skb_size;
		}
		TCP_SKB_CB(skb)->flags = flags;

		/* All done, get rid of second SKB and account for it so
		 * packet counting does not break.
		 */
		kfree_skb(next_skb);
		sk->tp_pinfo.af_tcp.packets_out--;
	}
}

/* Do a simple retransmit without using the backoff mechanisms in
 * tcp_timer. This is used for path mtu discovery. 
 * The socket is already locked here.
 */ 
void tcp_simple_retransmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb, *old_next_skb;
	unsigned int mss = tcp_current_mss(sk);

 	/* Don't muck with the congestion window here. */
 	tp->dup_acks = 0;
 	tp->high_seq = tp->snd_nxt;
 	tp->retrans_head = NULL;

 	/* Input control flow will see that this was retransmitted
	 * and not use it for RTT calculation in the absence of
	 * the timestamp option.
	 */
	for (old_next_skb = skb = skb_peek(&sk->write_queue);
	     ((skb != tp->send_head) &&
	      (skb != (struct sk_buff *)&sk->write_queue));
	     skb = skb->next) {
		int resend_skb = 0;

		/* Our goal is to push out the packets which we
		 * sent already, but are being chopped up now to
		 * account for the PMTU information we have.
		 *
		 * As we resend the queue, packets are fragmented
		 * into two pieces, and when we try to send the
		 * second piece it may be collapsed together with
		 * a subsequent packet, and so on.  -DaveM
		 */
		if (old_next_skb != skb || skb->len > mss)
			resend_skb = 1;
		old_next_skb = skb->next;
		if (resend_skb != 0)
			tcp_retransmit_skb(sk, skb);
	}
}

static __inline__ void update_retrans_head(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
	
	tp->retrans_head = tp->retrans_head->next;
	if((tp->retrans_head == tp->send_head) ||
	   (tp->retrans_head == (struct sk_buff *) &sk->write_queue)) {
		tp->retrans_head = NULL;
		tp->rexmt_done = 1;
	}
}

/* This retransmits one SKB.  Policy decisions and retransmit queue
 * state updates are done by the caller.  Returns non-zero if an
 * error occurred which prevented the send.
 */
int tcp_retransmit_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	unsigned int cur_mss = tcp_current_mss(sk);

	if(skb->len > cur_mss) {
		if(tcp_fragment(sk, skb, cur_mss))
			return 1; /* We'll try again later. */

		/* New SKB created, account for it. */
		tp->packets_out++;
	}

	/* Collapse two adjacent packets if worthwhile and we can. */
	if(!(TCP_SKB_CB(skb)->flags & TCPCB_FLAG_SYN) &&
	   (skb->len < (cur_mss >> 1)) &&
	   (skb->next != tp->send_head) &&
	   (skb->next != (struct sk_buff *)&sk->write_queue) &&
	   (sysctl_tcp_retrans_collapse != 0))
		tcp_retrans_try_collapse(sk, skb, cur_mss);

	if(tp->af_specific->rebuild_header(sk))
		return 1; /* Routing failure or similar. */

	/* Some Solaris stacks overoptimize and ignore the FIN on a
	 * retransmit when old data is attached.  So strip it off
	 * since it is cheap to do so and saves bytes on the network.
	 */
	if(skb->len > 0 &&
	   (TCP_SKB_CB(skb)->flags & TCPCB_FLAG_FIN) &&
	   tp->snd_una == (TCP_SKB_CB(skb)->end_seq - 1)) {
		TCP_SKB_CB(skb)->seq = TCP_SKB_CB(skb)->end_seq - 1;
		skb_trim(skb, 0);
		skb->csum = 0;
	}

	/* Ok, we're gonna send it out, update state. */
	TCP_SKB_CB(skb)->sacked |= TCPCB_SACKED_RETRANS;
	tp->retrans_out++;

	/* Make a copy, if the first transmission SKB clone we made
	 * is still in somebody's hands, else make a clone.
	 */
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	if(skb_cloned(skb))
		skb = skb_copy(skb, GFP_ATOMIC);
	else
		skb = skb_clone(skb, GFP_ATOMIC);

	tcp_transmit_skb(sk, skb);

	/* Update global TCP statistics and return success. */
	sk->prot->retransmits++;
	tcp_statistics.TcpRetransSegs++;

	return 0;
}

/* This gets called after a retransmit timeout, and the initially
 * retransmitted data is acknowledged.  It tries to continue
 * resending the rest of the retransmit queue, until either
 * we've sent it all or the congestion window limit is reached.
 * If doing SACK, the first ACK which comes back for a timeout
 * based retransmit packet might feed us FACK information again.
 * If so, we use it to avoid unnecessarily retransmissions.
 */
void tcp_xmit_retransmit_queue(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;

	if (tp->retrans_head == NULL &&
	    tp->rexmt_done == 0)
		tp->retrans_head = skb_peek(&sk->write_queue);
	if (tp->retrans_head == tp->send_head)
		tp->retrans_head = NULL;

	/* Each time, advance the retrans_head if we got
	 * a packet out or we skipped one because it was
	 * SACK'd.  -DaveM
	 */
	while ((skb = tp->retrans_head) != NULL) {
		/* If it has been ack'd by a SACK block, we don't
		 * retransmit it.
		 */
		if(!(TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_ACKED)) {
			/* Send it out, punt if error occurred. */
			if(tcp_retransmit_skb(sk, skb))
				break;

			update_retrans_head(sk);
		
			/* Stop retransmitting if we've hit the congestion
			 * window limit.
			 */
			if (tp->retrans_out >= tp->snd_cwnd)
				break;
		} else {
			update_retrans_head(sk);
		}
	}
}

/* Using FACK information, retransmit all missing frames at the receiver
 * up to the forward most SACK'd packet (tp->fackets_out) if the packet
 * has not been retransmitted already.
 */
void tcp_fack_retransmit(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb = skb_peek(&sk->write_queue);
	int packet_cnt = 0;

	while((skb != NULL) &&
	      (skb != tp->send_head) &&
	      (skb != (struct sk_buff *)&sk->write_queue)) {
		__u8 sacked = TCP_SKB_CB(skb)->sacked;

		if(sacked & (TCPCB_SACKED_ACKED | TCPCB_SACKED_RETRANS))
			goto next_packet;

		/* Ok, retransmit it. */
		if(tcp_retransmit_skb(sk, skb))
			break;

		if(tcp_packets_in_flight(tp) >= tp->snd_cwnd)
			break;
next_packet:
		packet_cnt++;
		if(packet_cnt >= tp->fackets_out)
			break;
		skb = skb->next;
	}
}

/* Send a fin.  The caller locks the socket for us.  This cannot be
 * allowed to fail queueing a FIN frame under any circumstances.
 */
void tcp_send_fin(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);	
	struct sk_buff *skb = skb_peek_tail(&sk->write_queue);
	unsigned int mss_now;
	
	/* Optimization, tack on the FIN if we have a queue of
	 * unsent frames.  But be careful about outgoing SACKS
	 * and IP options.
	 */
	mss_now = tcp_current_mss(sk); 

	if((tp->send_head != NULL) && (skb->len < mss_now)) {
		/* tcp_write_xmit() takes care of the rest. */
		TCP_SKB_CB(skb)->flags |= TCPCB_FLAG_FIN;
		TCP_SKB_CB(skb)->end_seq++;
		tp->write_seq++;

		/* Special case to avoid Nagle bogosity.  If this
		 * segment is the last segment, and it was queued
		 * due to Nagle/SWS-avoidance, send it out now.
		 */
		if(tp->send_head == skb &&
		   !sk->nonagle &&
		   skb->len < (tp->mss_cache >> 1) &&
		   tp->packets_out &&
		   !(TCP_SKB_CB(skb)->flags & TCPCB_FLAG_URG)) {
			update_send_head(sk);
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
			tp->packets_out++;
			tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
			if(!tcp_timer_is_set(sk, TIME_RETRANS))
				tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
		}
	} else {
		/* Socket is locked, keep trying until memory is available. */
		do {
			skb = sock_wmalloc(sk,
					   (MAX_HEADER +
					    sk->prot->max_header),
					   1, GFP_KERNEL);
		} while (skb == NULL);

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(skb, MAX_HEADER + sk->prot->max_header);
		skb->csum = 0;
		TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_FIN);
		TCP_SKB_CB(skb)->sacked = 0;
		TCP_SKB_CB(skb)->urg_ptr = 0;

		/* FIN eats a sequence byte, write_seq advanced by tcp_send_skb(). */
		TCP_SKB_CB(skb)->seq = tp->write_seq;
		TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
		tcp_send_skb(sk, skb, 0);
	}
}

/* We get here when a process closes a file descriptor (either due to
 * an explicit close() or as a byproduct of exit()'ing) and there
 * was unread data in the receive queue.  This behavior is recommended
 * by draft-ietf-tcpimpl-prob-03.txt section 3.10.  -DaveM
 */
void tcp_send_active_reset(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;

	/* NOTE: No TCP options attached and we never retransmit this. */
	skb = alloc_skb(MAX_HEADER + sk->prot->max_header, GFP_KERNEL);
	if (!skb)
		return;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, MAX_HEADER + sk->prot->max_header);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_RST);
	TCP_SKB_CB(skb)->sacked = 0;
	TCP_SKB_CB(skb)->urg_ptr = 0;

	/* Send it off. */
	TCP_SKB_CB(skb)->seq = tp->write_seq;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tcp_transmit_skb(sk, skb);
}

/* WARNING: This routine must only be called when we have already sent
 * a SYN packet that crossed the incoming SYN that caused this routine
 * to get called. If this assumption fails then the initial rcv_wnd
 * and rcv_wscale values will not be correct.
 */
int tcp_send_synack(struct sock *sk)
{
	struct tcp_opt* tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff* skb;	
	
	skb = sock_wmalloc(sk, (MAX_HEADER + sk->prot->max_header),
			   1, GFP_ATOMIC);
	if (skb == NULL) 
		return -ENOMEM;

	/* Reserve space for headers and prepare control bits. */
	skb_reserve(skb, MAX_HEADER + sk->prot->max_header);
	skb->csum = 0;
	TCP_SKB_CB(skb)->flags = (TCPCB_FLAG_ACK | TCPCB_FLAG_SYN);
	TCP_SKB_CB(skb)->sacked = 0;
	TCP_SKB_CB(skb)->urg_ptr = 0;

	/* SYN eats a sequence byte. */
	TCP_SKB_CB(skb)->seq = tp->snd_una;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
	__skb_queue_tail(&sk->write_queue, skb);
	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tp->packets_out++;
	tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
	return 0;
}

/*
 * Prepare a SYN-ACK.
 */
struct sk_buff * tcp_make_synack(struct sock *sk, struct dst_entry *dst,
				 struct open_request *req, int mss)
{
	struct tcphdr *th;
	int tcp_header_size;
	struct sk_buff *skb;

	skb = sock_wmalloc(sk, MAX_HEADER + sk->prot->max_header, 1, GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	/* Reserve space for headers. */
	skb_reserve(skb, MAX_HEADER + sk->prot->max_header);

	skb->dst = dst_clone(dst);

	/* Don't offer more than they did.
	 * This way we don't have to memorize who said what.
	 * FIXME: maybe this should be changed for better performance
	 * with syncookies.
	 */
	req->mss = min(mss, req->mss);
	if (req->mss < 8) {
		printk(KERN_DEBUG "initial req->mss below 8\n");
		req->mss = 8;
	}

	tcp_header_size = (sizeof(struct tcphdr) + TCPOLEN_MSS +
			   (req->tstamp_ok ? TCPOLEN_TSTAMP_ALIGNED : 0) +
			   (req->wscale_ok ? TCPOLEN_WSCALE_ALIGNED : 0) +
			   /* SACK_PERM is in the place of NOP NOP of TS */
			   ((req->sack_ok && !req->tstamp_ok) ? TCPOLEN_SACKPERM_ALIGNED : 0));
	skb->h.th = th = (struct tcphdr *) skb_push(skb, tcp_header_size);

	memset(th, 0, sizeof(struct tcphdr));
	th->syn = 1;
	th->ack = 1;
	th->source = sk->sport;
	th->dest = req->rmt_port;
	TCP_SKB_CB(skb)->seq = req->snt_isn;
	TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq + 1;
	th->seq = htonl(TCP_SKB_CB(skb)->seq);
	th->ack_seq = htonl(req->rcv_isn + 1);
	if (req->rcv_wnd == 0) { /* ignored for retransmitted syns */
		__u8 rcv_wscale; 
		/* Set this up on the first call only */
		req->window_clamp = skb->dst->window;
		tcp_select_initial_window(sock_rspace(sk)/2,req->mss,
			&req->rcv_wnd,
			&req->window_clamp,
			req->wscale_ok,
			&rcv_wscale);
		req->rcv_wscale = rcv_wscale; 
	}

	/* RFC1323: The window in SYN & SYN/ACK segments is never scaled. */
	th->window = htons(req->rcv_wnd);

	TCP_SKB_CB(skb)->when = tcp_time_stamp;
	tcp_syn_build_options((__u32 *)(th + 1), req->mss, req->tstamp_ok,
			      req->sack_ok, req->wscale_ok, req->rcv_wscale,
			      TCP_SKB_CB(skb)->when,
			      req->ts_recent);

	skb->csum = 0;
	th->doff = (tcp_header_size >> 2);
	tcp_statistics.TcpOutSegs++; 
	return skb;
}

void tcp_connect(struct sock *sk, struct sk_buff *buff, int mtu)
{
	struct dst_entry *dst = sk->dst_cache;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Reserve space for headers. */
	skb_reserve(buff, MAX_HEADER + sk->prot->max_header);

	tp->snd_wnd = 0;
	tp->snd_wl1 = 0;
	tp->snd_wl2 = tp->write_seq;
	tp->snd_una = tp->write_seq;
	tp->rcv_nxt = 0;

	sk->err = 0;
	sk->done = 0;
	
	/* We'll fix this up when we get a response from the other end.
	 * See tcp_input.c:tcp_rcv_state_process case TCP_SYN_SENT.
	 */
	tp->tcp_header_len = sizeof(struct tcphdr) +
		(sysctl_tcp_timestamps ? TCPOLEN_TSTAMP_ALIGNED : 0);

	/* If user gave his TCP_MAXSEG, record it to clamp */
	if (tp->user_mss)
		tp->mss_clamp = tp->user_mss;
	tcp_sync_mss(sk, mtu);

	/* Now unpleasant action: if initial pmtu is too low
	   set lower clamp. I am not sure that it is good.
	   To be more exact, I do not think that clamping at value, which
	   is apparently transient and may improve in future is good idea.
	   It would be better to wait until peer will returns its MSS
	   (probably 65535 too) and now advertise something sort of 65535
	   or at least first hop device mtu. Is it clear, what I mean?
	   We should tell peer what maximal mss we expect to RECEIVE,
	   it has nothing to do with pmtu.
	   I am afraid someone will be confused by such huge value.
	                                                   --ANK (980731)
	 */
	if (tp->mss_cache + tp->tcp_header_len - sizeof(struct tcphdr) < tp->mss_clamp )
		tp->mss_clamp = tp->mss_cache + tp->tcp_header_len - sizeof(struct tcphdr);

	TCP_SKB_CB(buff)->flags = TCPCB_FLAG_SYN;
	TCP_SKB_CB(buff)->sacked = 0;
	TCP_SKB_CB(buff)->urg_ptr = 0;
	buff->csum = 0;
	TCP_SKB_CB(buff)->seq = tp->write_seq++;
	TCP_SKB_CB(buff)->end_seq = tp->write_seq;
	tp->snd_nxt = TCP_SKB_CB(buff)->end_seq;

	tp->window_clamp = dst->window;
	tcp_select_initial_window(sock_rspace(sk)/2,tp->mss_clamp,
		&tp->rcv_wnd,
		&tp->window_clamp,
		sysctl_tcp_window_scaling,
		&tp->rcv_wscale);
	/* Ok, now lock the socket before we make it visible to
	 * the incoming packet engine.
	 */
	lock_sock(sk);

	/* Socket identity change complete, no longer
	 * in TCP_CLOSE, so enter ourselves into the
	 * hash tables.
	 */
	tcp_set_state(sk,TCP_SYN_SENT);
	sk->prot->hash(sk);

	tp->rto = dst->rtt;
	tcp_init_xmit_timers(sk);
	tp->retransmits = 0;
	tp->fackets_out = 0;
	tp->retrans_out = 0;

	/* Send it off. */
	__skb_queue_tail(&sk->write_queue, buff);
	TCP_SKB_CB(buff)->when = tcp_time_stamp;
	tp->packets_out++;
	tcp_transmit_skb(sk, skb_clone(buff, GFP_KERNEL));
	tcp_statistics.TcpActiveOpens++;

	/* Timer for repeating the SYN until an answer. */
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

	/* Now, it is safe to release the socket. */
	release_sock(sk);
}

/* Send out a delayed ack, the caller does the policy checking
 * to see if we should even be here.  See tcp_input.c:tcp_ack_snd_check()
 * for details.
 */
void tcp_send_delayed_ack(struct tcp_opt *tp, int max_timeout)
{
	unsigned long timeout;

	/* Stay within the limit we were given */
	timeout = tp->ato;
	if (timeout > max_timeout)
		timeout = max_timeout;
	timeout += jiffies;

	/* Use new timeout only if there wasn't a older one earlier. */
	if (!tp->delack_timer.prev) {
		tp->delack_timer.expires = timeout;
		add_timer(&tp->delack_timer);
        } else {
		if (time_before(timeout, tp->delack_timer.expires))
			mod_timer(&tp->delack_timer, timeout);
	}
}

/* This routine sends an ack and also updates the window. */
void tcp_send_ack(struct sock *sk)
{
	/* If we have been reset, we may not send again. */
	if(!sk->zapped) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct sk_buff *buff;

		/* We are not putting this on the write queue, so
		 * tcp_transmit_skb() will set the ownership to this
		 * sock.
		 */
		buff = alloc_skb(MAX_HEADER + sk->prot->max_header, GFP_ATOMIC);
		if (buff == NULL) {
			/* Force it to send an ack. We don't have to do this
			 * (ACK is unreliable) but it's much better use of
			 * bandwidth on slow links to send a spare ack than
			 * resend packets.
			 *
			 * This is the one possible way that we can delay an
			 * ACK and have tp->ato indicate that we are in
			 * quick ack mode, so clear it.
			 */
			if(tcp_in_quickack_mode(tp))
				tcp_exit_quickack_mode(tp);
			tcp_send_delayed_ack(tp, HZ/2);
			return;
		}

		/* Reserve space for headers and prepare control bits. */
		skb_reserve(buff, MAX_HEADER + sk->prot->max_header);
		buff->csum = 0;
		TCP_SKB_CB(buff)->flags = TCPCB_FLAG_ACK;
		TCP_SKB_CB(buff)->sacked = 0;
		TCP_SKB_CB(buff)->urg_ptr = 0;

		/* Send it off, this clears delayed acks for us. */
		TCP_SKB_CB(buff)->seq = TCP_SKB_CB(buff)->end_seq = tp->snd_nxt;
		TCP_SKB_CB(buff)->when = tcp_time_stamp;
		tcp_transmit_skb(sk, buff);
	}
}

/* This routine sends a packet with an out of date sequence
 * number. It assumes the other end will try to ack it.
 */
void tcp_write_wakeup(struct sock *sk)
{
	/* After a valid reset we can send no more. */
	if (!sk->zapped) {
		struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
		struct sk_buff *skb;

		/* Write data can still be transmitted/retransmitted in the
		 * following states.  If any other state is encountered, return.
		 * [listen/close will never occur here anyway]
		 */
		if ((1 << sk->state) &
		    ~(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_FIN_WAIT1|
		      TCPF_LAST_ACK|TCPF_CLOSING))
			return;

		if (before(tp->snd_nxt, tp->snd_una + tp->snd_wnd) &&
		    ((skb = tp->send_head) != NULL)) {
			unsigned long win_size;

			/* We are probing the opening of a window
			 * but the window size is != 0
			 * must have been a result SWS avoidance ( sender )
			 */
			win_size = tp->snd_wnd - (tp->snd_nxt - tp->snd_una);
			if (win_size < TCP_SKB_CB(skb)->end_seq - TCP_SKB_CB(skb)->seq) {
				if (tcp_fragment(sk, skb, win_size))
					return; /* Let a retransmit get it. */
			}
			update_send_head(sk);
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			tp->snd_nxt = TCP_SKB_CB(skb)->end_seq;
			tp->packets_out++;
			tcp_transmit_skb(sk, skb_clone(skb, GFP_ATOMIC));
			if (!tcp_timer_is_set(sk, TIME_RETRANS))
				tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
		} else {
			/* We don't queue it, tcp_transmit_skb() sets ownership. */
			skb = alloc_skb(MAX_HEADER + sk->prot->max_header,
					GFP_ATOMIC);
			if (skb == NULL) 
				return;

			/* Reserve space for headers and set control bits. */
			skb_reserve(skb, MAX_HEADER + sk->prot->max_header);
			skb->csum = 0;
			TCP_SKB_CB(skb)->flags = TCPCB_FLAG_ACK;
			TCP_SKB_CB(skb)->sacked = 0;
			TCP_SKB_CB(skb)->urg_ptr = 0;

			/* Use a previous sequence.  This should cause the other
			 * end to send an ack.  Don't queue or clone SKB, just
			 * send it.
			 */
			TCP_SKB_CB(skb)->seq = tp->snd_nxt - 1;
			TCP_SKB_CB(skb)->end_seq = TCP_SKB_CB(skb)->seq;
			TCP_SKB_CB(skb)->when = tcp_time_stamp;
			tcp_transmit_skb(sk, skb);
		}
	}
}

/* A window probe timeout has occurred.  If window is not closed send
 * a partial packet else a zero probe.
 */
void tcp_send_probe0(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tcp_write_wakeup(sk);
	tp->pending = TIME_PROBE0;
	tp->backoff++;
	tp->probes_out++;
	tcp_reset_xmit_timer (sk, TIME_PROBE0, 
			      min(tp->rto << tp->backoff, 120*HZ));
}
