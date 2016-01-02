/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_input.c,v 1.164.2.8 1999/09/23 19:21:23 davem Exp $
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
 * Changes:
 *		Pedro Roque	:	Fast Retransmit/Recovery.
 *					Two receive queues.
 *					Retransmit queue handled by TCP.
 *					Better retransmit timer handling.
 *					New congestion avoidance.
 *					Header prediction.
 *					Variable renaming.
 *
 *		Eric		:	Fast Retransmit.
 *		Randy Scott	:	MSS option defines.
 *		Eric Schenk	:	Fixes to slow start algorithm.
 *		Eric Schenk	:	Yet another double ACK bug.
 *		Eric Schenk	:	Delayed ACK bug fixes.
 *		Eric Schenk	:	Floyd style fast retrans war avoidance.
 *		David S. Miller	:	Don't allow zero congestion window.
 *		Eric Schenk	:	Fix retransmitter so that it sends
 *					next packet on ack of previous packet.
 *		Andi Kleen	:	Moved open_request checking here
 *					and process RSTs for open_requests.
 *		Andi Kleen	:	Better prune_queue, and other fixes.
 *		Andrey Savochkin:	Fix RTT measurements in the presnce of
 *					timestamps.
 *		Andrey Savochkin:	Check sequence numbers correctly when
 *					removing SACKs due to in sequence incoming
 *					data segments.
 *		Andi Kleen:		Make sure we never ack data there is not
 *					enough room for. Also make this condition
 *					a fatal error if it might still happen.
 *		Andi Kleen:		Add tcp_measure_rcv_mss to make 
 *					connections with MSS<min(MTU,ann. MSS)
 *					work without delayed acks. 
 *		Andi Kleen:		Process packets with PSH set in the
 *					fast path.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/tcp.h>
#include <linux/ipsec.h>

#ifdef CONFIG_SYSCTL
#define SYNC_INIT 0 /* let the user enable it */
#else
#define SYNC_INIT 1
#endif

extern int sysctl_tcp_fin_timeout;

/* These are on by default so the code paths get tested.
 * For the final 2.2 this may be undone at our discretion. -DaveM
 */
int sysctl_tcp_timestamps = 1;
int sysctl_tcp_window_scaling = 1;
int sysctl_tcp_sack = 1;

int sysctl_tcp_syncookies = SYNC_INIT; 
int sysctl_tcp_stdurg;
int sysctl_tcp_rfc1337;

static int prune_queue(struct sock *sk);

/* There is something which you must keep in mind when you analyze the
 * behavior of the tp->ato delayed ack timeout interval.  When a
 * connection starts up, we want to ack as quickly as possible.  The
 * problem is that "good" TCP's do slow start at the beginning of data
 * transmission.  The means that until we send the first few ACK's the
 * sender will sit on his end and only queue most of his data, because
 * he can only send snd_cwnd unacked packets at any given time.  For
 * each ACK we send, he increments snd_cwnd and transmits more of his
 * queue.  -DaveM
 */
static void tcp_delack_estimator(struct tcp_opt *tp)
{
	if(tp->ato == 0) {
		tp->lrcvtime = tcp_time_stamp;

		/* Help sender leave slow start quickly,
		 * and also makes sure we do not take this
		 * branch ever again for this connection.
		 */
		tp->ato = 1;
		tcp_enter_quickack_mode(tp);
	} else {
		int m = tcp_time_stamp - tp->lrcvtime;

		tp->lrcvtime = tcp_time_stamp;
		if(m <= 0)
			m = 1;
		if(m > tp->rto)
			tp->ato = tp->rto;
		else {
			/* This funny shift makes sure we
			 * clear the "quick ack mode" bit.
			 */
			tp->ato = ((tp->ato << 1) >> 2) + m;
		}
	}
}

/* 
 * Remember to send an ACK later.
 */
static __inline__ void tcp_remember_ack(struct tcp_opt *tp, struct tcphdr *th, 
					struct sk_buff *skb)
{
	tp->delayed_acks++; 

	/* Tiny-grams with PSH set artificially deflate our
	 * ato measurement, but with a lower bound.
	 */
	if(th->psh && (skb->len < (tp->mss_cache >> 1))) {
		/* Preserve the quickack state. */
		if((tp->ato & 0x7fffffff) > HZ/50)
			tp->ato = ((tp->ato & 0x80000000) |
				   (HZ/50));
	}
} 

/* Called to compute a smoothed rtt estimate. The data fed to this
 * routine either comes from timestamps, or from segments that were
 * known _not_ to have been retransmitted [see Karn/Partridge
 * Proceedings SIGCOMM 87]. The algorithm is from the SIGCOMM 88
 * piece by Van Jacobson.
 * NOTE: the next three routines used to be one big routine.
 * To save cycles in the RFC 1323 implementation it was better to break
 * it up into three procedures. -- erics
 */

static __inline__ void tcp_rtt_estimator(struct tcp_opt *tp, __u32 mrtt)
{
	long m = mrtt; /* RTT */

	/*	The following amusing code comes from Jacobson's
	 *	article in SIGCOMM '88.  Note that rtt and mdev
	 *	are scaled versions of rtt and mean deviation.
	 *	This is designed to be as fast as possible 
	 *	m stands for "measurement".
	 *
	 *	On a 1990 paper the rto value is changed to:
	 *	RTO = rtt + 4 * mdev
	 */
	if(m == 0)
		m = 1;
	if (tp->srtt != 0) {
		m -= (tp->srtt >> 3);	/* m is now error in rtt est */
		tp->srtt += m;		/* rtt = 7/8 rtt + 1/8 new */
		if (m < 0)
			m = -m;		/* m is now abs(error) */
		m -= (tp->mdev >> 2);   /* similar update on mdev */
		tp->mdev += m;	    	/* mdev = 3/4 mdev + 1/4 new */
	} else {
		/* no previous measure. */
		tp->srtt = m<<3;	/* take the measured time to be rtt */
		tp->mdev = m<<2;	/* make sure rto = 3*rtt */
	}
}

/* Calculate rto without backoff.  This is the second half of Van Jacobson's
 * routine referred to above.
 */

static __inline__ void tcp_set_rto(struct tcp_opt *tp)
{
	tp->rto = (tp->srtt >> 3) + tp->mdev;
	tp->rto += (tp->rto >> 2) + (tp->rto >> (tp->snd_cwnd-1));
}
 

/* Keep the rto between HZ/5 and 120*HZ. 120*HZ is the upper bound
 * on packet lifetime in the internet. We need the HZ/5 lower
 * bound to behave correctly against BSD stacks with a fixed
 * delayed ack.
 * FIXME: It's not entirely clear this lower bound is the best
 * way to avoid the problem. Is it possible to drop the lower
 * bound and still avoid trouble with BSD stacks? Perhaps
 * some modification to the RTO calculation that takes delayed
 * ack bias into account? This needs serious thought. -- erics
 */
static __inline__ void tcp_bound_rto(struct tcp_opt *tp)
{
	if (tp->rto > 120*HZ)
		tp->rto = 120*HZ;
	if (tp->rto < HZ/5)
		tp->rto = HZ/5;
}

/* WARNING: this must not be called if tp->saw_timestamp was false. */
static __inline__ void tcp_replace_ts_recent(struct sock *sk, struct tcp_opt *tp,
					     __u32 start_seq, __u32 end_seq)
{
	/* It is start_seq <= last_ack_seq combined
	   with in window check. If start_seq<=last_ack_seq<=rcv_nxt,
	   then segment is in window if end_seq>=rcv_nxt.
	 */
	if (!after(start_seq, tp->last_ack_sent) &&
	    !before(end_seq, tp->rcv_nxt)) {
		/* PAWS bug workaround wrt. ACK frames, the PAWS discard
		 * extra check below makes sure this can only happen
		 * for pure ACK frames.  -DaveM
		 *
		 * Plus: expired timestamps.
		 *
		 * Plus: resets failing PAWS.
		 */
		if((s32)(tp->rcv_tsval - tp->ts_recent) >= 0) {
			tp->ts_recent = tp->rcv_tsval;
			tp->ts_recent_stamp = tcp_time_stamp;
		}
	}
}

#define PAWS_24DAYS	(HZ * 60 * 60 * 24 * 24)

static __inline__ int tcp_paws_discard(struct tcp_opt *tp, struct tcphdr *th, unsigned len)
{
	return ((s32)(tp->rcv_tsval - tp->ts_recent) < 0 &&
		(s32)(tcp_time_stamp - tp->ts_recent_stamp) < PAWS_24DAYS &&
		/* Sorry, PAWS as specified is broken wrt. pure-ACKs -DaveM */
		len != (th->doff * 4));
}


static int __tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
	u32 end_window = tp->rcv_wup + tp->rcv_wnd;

	if (tp->rcv_wnd &&
	    after(end_seq, tp->rcv_nxt) &&
	    before(seq, end_window))
		return 1;
	if (seq != end_window)
		return 0;
	return (seq == end_seq);
}

/* This functions checks to see if the tcp header is actually acceptable. */
static __inline__ int tcp_sequence(struct tcp_opt *tp, u32 seq, u32 end_seq)
{
	if (seq == tp->rcv_nxt)
		return (tp->rcv_wnd || (end_seq == seq));

	return __tcp_sequence(tp, seq, end_seq);
}

/* When we get a reset we do this. */
static void tcp_reset(struct sock *sk)
{
	sk->zapped = 1;

	/* We want the right error as BSD sees it (and indeed as we do). */
	switch (sk->state) {
		case TCP_SYN_SENT:
			sk->err = ECONNREFUSED;
			break;
		case TCP_CLOSE_WAIT:
			sk->err = EPIPE;
			break;
		default:
			sk->err = ECONNRESET;
	};
	tcp_set_state(sk, TCP_CLOSE);
	sk->shutdown = SHUTDOWN_MASK;
	if (!sk->dead) 
		sk->state_change(sk);
}

/* This tags the retransmission queue when SACKs arrive. */
static void tcp_sacktag_write_queue(struct sock *sk, struct tcp_sack_block *sp, int nsacks)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int i = nsacks;

	while(i--) {
		struct sk_buff *skb = skb_peek(&sk->write_queue);
		__u32 start_seq = ntohl(sp->start_seq);
		__u32 end_seq = ntohl(sp->end_seq);
		int fack_count = 0;

		while((skb != NULL) &&
		      (skb != tp->send_head) &&
		      (skb != (struct sk_buff *)&sk->write_queue)) {
			/* The retransmission queue is always in order, so
			 * we can short-circuit the walk early.
			 */
			if(after(TCP_SKB_CB(skb)->seq, end_seq))
				break;

			/* We play conservative, we don't allow SACKS to partially
			 * tag a sequence space.
			 */
			fack_count++;
			if(!after(start_seq, TCP_SKB_CB(skb)->seq) &&
			   !before(end_seq, TCP_SKB_CB(skb)->end_seq)) {
				/* If this was a retransmitted frame, account for it. */
				if((TCP_SKB_CB(skb)->sacked & TCPCB_SACKED_RETRANS) &&
				   tp->retrans_out)
					tp->retrans_out--;
				TCP_SKB_CB(skb)->sacked |= TCPCB_SACKED_ACKED;

				/* RULE: All new SACKs will either decrease retrans_out
				 *       or advance fackets_out.
				 */
				if(fack_count > tp->fackets_out)
					tp->fackets_out = fack_count;
			}
			skb = skb->next;
		}
		sp++; /* Move on to the next SACK block. */
	}
}

/* Look for tcp options. Normally only called on SYN and SYNACK packets.
 * But, this can also be called on packets in the established flow when
 * the fast version below fails.
 */
void tcp_parse_options(struct sock *sk, struct tcphdr *th, struct tcp_opt *tp, int no_fancy)
{
	unsigned char *ptr;
	int length=(th->doff*4)-sizeof(struct tcphdr);
	int saw_mss = 0;

	ptr = (unsigned char *)(th + 1);
	tp->saw_tstamp = 0;

	while(length>0) {
	  	int opcode=*ptr++;
		int opsize;

		switch (opcode) {
			case TCPOPT_EOL:
				return;
			case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
				length--;
				continue;
			default:
				opsize=*ptr++;
				if (opsize < 2) /* "silly options" */
					return;
				if (opsize > length)
					break;	/* don't parse partial options */
	  			switch(opcode) {
				case TCPOPT_MSS:
					if(opsize==TCPOLEN_MSS && th->syn) {
						u16 in_mss = ntohs(*(__u16 *)ptr);
						if (in_mss == 0)
							in_mss = 536;
						if (tp->mss_clamp > in_mss)
							tp->mss_clamp = in_mss;
						saw_mss = 1;
					}
					break;
				case TCPOPT_WINDOW:
					if(opsize==TCPOLEN_WINDOW && th->syn)
						if (!no_fancy && sysctl_tcp_window_scaling) {
							tp->wscale_ok = 1;
							tp->snd_wscale = *(__u8 *)ptr;
							if(tp->snd_wscale > 14) {
								if(net_ratelimit())
									printk("tcp_parse_options: Illegal window "
									       "scaling value %d >14 received.",
									       tp->snd_wscale);
								tp->snd_wscale = 14;
							}
						}
					break;
				case TCPOPT_TIMESTAMP:
					if(opsize==TCPOLEN_TIMESTAMP) {
						if (sysctl_tcp_timestamps && !no_fancy) {
							tp->tstamp_ok = 1;
							tp->saw_tstamp = 1;
							tp->rcv_tsval = ntohl(*(__u32 *)ptr);
							tp->rcv_tsecr = ntohl(*(__u32 *)(ptr+4));
						}
					}
					break;
				case TCPOPT_SACK_PERM:
					if(opsize==TCPOLEN_SACK_PERM && th->syn) {
						if (sysctl_tcp_sack && !no_fancy) {
							tp->sack_ok = 1;
							tp->num_sacks = 0;
						}
					}
					break;

				case TCPOPT_SACK:
					if((opsize >= (TCPOLEN_SACK_BASE + TCPOLEN_SACK_PERBLOCK)) &&
					   sysctl_tcp_sack && (sk != NULL) && !th->syn) {
						int sack_bytes = opsize - TCPOLEN_SACK_BASE;

						if(!(sack_bytes % TCPOLEN_SACK_PERBLOCK)) {
							int num_sacks = sack_bytes >> 3;
							struct tcp_sack_block *sackp;

							sackp = (struct tcp_sack_block *)ptr;
							tcp_sacktag_write_queue(sk, sackp, num_sacks);
						}
					}
	  			};
	  			ptr+=opsize-2;
	  			length-=opsize;
	  	};
	}
	if(th->syn && saw_mss == 0)
		tp->mss_clamp = 536;
}

/* Fast parse options. This hopes to only see timestamps.
 * If it is wrong it falls back on tcp_parse_options().
 */
static __inline__ int tcp_fast_parse_options(struct sock *sk, struct tcphdr *th, struct tcp_opt *tp)
{
	/* If we didn't send out any options ignore them all. */
	if (tp->tcp_header_len == sizeof(struct tcphdr))
		return 0;
	if (th->doff == sizeof(struct tcphdr)>>2) {
		tp->saw_tstamp = 0;
		return 0;
	} else if (th->doff == (sizeof(struct tcphdr)>>2)+(TCPOLEN_TSTAMP_ALIGNED>>2)) {
		__u32 *ptr = (__u32 *)(th + 1);
		if (*ptr == __constant_ntohl((TCPOPT_NOP << 24) | (TCPOPT_NOP << 16)
					     | (TCPOPT_TIMESTAMP << 8) | TCPOLEN_TIMESTAMP)) {
			tp->saw_tstamp = 1;
			tp->rcv_tsval = ntohl(*++ptr);
			tp->rcv_tsecr = ntohl(*++ptr);
			return 1;
		}
	}
	tcp_parse_options(sk, th, tp, 0);
	return 1;
}

#define FLAG_DATA		0x01 /* Incoming frame contained data.		*/
#define FLAG_WIN_UPDATE		0x02 /* Incoming ACK was a window update.	*/
#define FLAG_DATA_ACKED		0x04 /* This ACK acknowledged new data.		*/
#define FLAG_RETRANS_DATA_ACKED	0x08 /* "" "" some of which was retransmitted.	*/

static __inline__ void clear_fast_retransmit(struct tcp_opt *tp)
{
	if (tp->dup_acks > 3)
		tp->snd_cwnd = (tp->snd_ssthresh);

	tp->dup_acks = 0;
}

/* NOTE: This code assumes that tp->dup_acks gets cleared when a
 * retransmit timer fires.
 */
static void tcp_fast_retrans(struct sock *sk, u32 ack, int not_dup)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Note: If not_dup is set this implies we got a
	 * data carrying packet or a window update.
	 * This carries no new information about possible
	 * lost packets, so we have to ignore it for the purposes
	 * of counting duplicate acks. Ideally this does not imply we
	 * should stop our fast retransmit phase, more acks may come
	 * later without data to help us. Unfortunately this would make
	 * the code below much more complex. For now if I see such
	 * a packet I clear the fast retransmit phase.
	 */
	if (ack == tp->snd_una && tp->packets_out && (not_dup == 0)) {
		/* This is the standard reno style fast retransmit branch. */

                /* 1. When the third duplicate ack is received, set ssthresh 
                 * to one half the current congestion window, but no less 
                 * than two segments. Retransmit the missing segment.
                 */
		if (tp->high_seq == 0 || after(ack, tp->high_seq)) {
			tp->dup_acks++;
			if ((tp->fackets_out > 3) || (tp->dup_acks == 3)) {
                                tp->snd_ssthresh = tcp_recalc_ssthresh(tp);
                                tp->snd_cwnd = (tp->snd_ssthresh + 3);
				tp->high_seq = tp->snd_nxt;
				if(!tp->fackets_out)
					tcp_retransmit_skb(sk,
							   skb_peek(&sk->write_queue));
				else
					tcp_fack_retransmit(sk);
                                tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
			}
		} else if (++tp->dup_acks > 3) {
			/* 2. Each time another duplicate ACK arrives, increment 
			 * cwnd by the segment size. [...] Transmit a packet...
			 *
			 * Packet transmission will be done on normal flow processing
			 * since we're not in "retransmit mode".  We do not use
			 * duplicate ACKs to artificially inflate the congestion
			 * window when doing FACK.
			 */
			if(!tp->fackets_out) {
				tp->snd_cwnd++;
			} else {
				/* Fill any further holes which may have
				 * appeared.
				 *
				 * We may want to change this to run every
				 * further multiple-of-3 dup ack increments,
				 * to be more robust against out-of-order
				 * packet delivery.  -DaveM
				 */
				tcp_fack_retransmit(sk);
			}
		}
	} else if (tp->high_seq != 0) {
		/* In this branch we deal with clearing the Floyd style
		 * block on duplicate fast retransmits, and if requested
		 * we do Hoe style secondary fast retransmits.
		 */
		if (!before(ack, tp->high_seq) || (not_dup & FLAG_DATA) != 0) {
			/* Once we have acked all the packets up to high_seq
			 * we are done this fast retransmit phase.
			 * Alternatively data arrived. In this case we
			 * Have to abort the fast retransmit attempt.
			 * Note that we do want to accept a window
			 * update since this is expected with Hoe's algorithm.
			 */
			clear_fast_retransmit(tp);

			/* After we have cleared up to high_seq we can
			 * clear the Floyd style block.
			 */
			if (!before(ack, tp->high_seq)) {
				tp->high_seq = 0;
				tp->fackets_out = 0;
			}
		} else if (tp->dup_acks >= 3) {
			if (!tp->fackets_out) {
				/* Hoe Style. We didn't ack the whole
				 * window. Take this as a cue that
				 * another packet was lost and retransmit it.
				 * Don't muck with the congestion window here.
				 * Note that we have to be careful not to
				 * act if this was a window update and it
				 * didn't ack new data, since this does
				 * not indicate a packet left the system.
				 * We can test this by just checking
				 * if ack changed from snd_una, since
				 * the only way to get here without advancing
				 * from snd_una is if this was a window update.
				 */
				if (ack != tp->snd_una && before(ack, tp->high_seq)) {
                                	tcp_retransmit_skb(sk,
							   skb_peek(&sk->write_queue));
                                	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
				}
			} else {
				/* FACK style, fill any remaining holes in
				 * receiver's queue.
				 */
				tcp_fack_retransmit(sk);
			}
		}
	}
}

/* This is Jacobson's slow start and congestion avoidance. 
 * SIGCOMM '88, p. 328.
 */
static __inline__ void tcp_cong_avoid(struct tcp_opt *tp)
{
        if (tp->snd_cwnd <= tp->snd_ssthresh) {
                /* In "safe" area, increase. */
                tp->snd_cwnd++;
	} else {
                /* In dangerous area, increase slowly.
		 * In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd
		 */
		if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
			tp->snd_cwnd++;
			tp->snd_cwnd_cnt=0;
		} else
			tp->snd_cwnd_cnt++;
        }       
}

/* Remove acknowledged frames from the retransmission queue. */
static int tcp_clean_rtx_queue(struct sock *sk, __u32 ack,
			       __u32 *seq, __u32 *seq_rtt)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sk_buff *skb;
	__u32 now = tcp_time_stamp;
	int acked = 0;

	/* If we are retransmitting, and this ACK clears up to
	 * the retransmit head, or further, then clear our state.
	 */
	if (tp->retrans_head != NULL &&
	    !before(ack, TCP_SKB_CB(tp->retrans_head)->end_seq))
		tp->retrans_head = NULL;

	while((skb=skb_peek(&sk->write_queue)) && (skb != tp->send_head)) {
		struct tcp_skb_cb *scb = TCP_SKB_CB(skb); 
		__u8 sacked = scb->sacked;
		
		/* If our packet is before the ack sequence we can
		 * discard it as it's confirmed to have arrived at
		 * the other end.
		 */
		if (after(scb->end_seq, ack))
			break;

		/* Initial outgoing SYN's get put onto the write_queue
		 * just like anything else we transmit.  It is not
		 * true data, and if we misinform our callers that
		 * this ACK acks real data, we will erroneously exit
		 * connection startup slow start one packet too
		 * quickly.  This is severely frowned upon behavior.
		 */
		if((sacked & TCPCB_SACKED_RETRANS) && tp->retrans_out)
			tp->retrans_out--;
		if(!(scb->flags & TCPCB_FLAG_SYN)) {
			acked |= FLAG_DATA_ACKED;
			if(sacked & TCPCB_SACKED_RETRANS)
				acked |= FLAG_RETRANS_DATA_ACKED;
			if(tp->fackets_out)
				tp->fackets_out--;
		} else {
			/* This is pure paranoia. */
			tp->retrans_head = NULL;
		}		
		tp->packets_out--;
		*seq = scb->seq;
		*seq_rtt = now - scb->when;
		__skb_unlink(skb, skb->list);
		kfree_skb(skb);
	}
	return acked;
}

static void tcp_ack_probe(struct sock *sk, __u32 ack)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	
	/* Our probe was answered. */
	tp->probes_out = 0;
	
	/* Was it a usable window open? */

	/* should always be non-null */
	if (tp->send_head != NULL &&
	    !before (ack + tp->snd_wnd, TCP_SKB_CB(tp->send_head)->end_seq)) {
		tp->backoff = 0;
		tp->pending = 0;
		tcp_clear_xmit_timer(sk, TIME_PROBE0);
	} else {
		tcp_reset_xmit_timer(sk, TIME_PROBE0,
				     min(tp->rto << tp->backoff, 120*HZ));
	}
}
 
/* Should we open up the congestion window? */
static __inline__ int should_advance_cwnd(struct tcp_opt *tp, int flag)
{
	/* Data must have been acked. */
	if ((flag & FLAG_DATA_ACKED) == 0)
		return 0;

	/* Some of the data acked was retransmitted somehow? */
	if ((flag & FLAG_RETRANS_DATA_ACKED) != 0) {
		/* We advance in all cases except during
		 * non-FACK fast retransmit/recovery.
		 */
		if (tp->fackets_out != 0 ||
		    tp->retransmits != 0)
			return 1;

		/* Non-FACK fast retransmit does it's own
		 * congestion window management, don't get
		 * in the way.
		 */
		return 0;
	}

	/* New non-retransmitted data acked, always advance.  */
	return 1;
}

/* Read draft-ietf-tcplw-high-performance before mucking
 * with this code. (Superceeds RFC1323)
 */
static void tcp_ack_saw_tstamp(struct sock *sk, struct tcp_opt *tp,
			       u32 seq, u32 ack, int flag)
{
	__u32 seq_rtt;

	/* RTTM Rule: A TSecr value received in a segment is used to
	 * update the averaged RTT measurement only if the segment
	 * acknowledges some new data, i.e., only if it advances the
	 * left edge of the send window.
	 *
	 * See draft-ietf-tcplw-high-performance-00, section 3.3.
	 * 1998/04/10 Andrey V. Savochkin <saw@msu.ru>
	 */
	if (!(flag & FLAG_DATA_ACKED))
		return;

	seq_rtt = tcp_time_stamp - tp->rcv_tsecr;
	tcp_rtt_estimator(tp, seq_rtt);
	if (tp->retransmits) {
		if (tp->packets_out == 0) {
			tp->retransmits = 0;
			tp->fackets_out = 0;
			tp->retrans_out = 0;
			tp->backoff = 0;
			tcp_set_rto(tp);
		} else {
			/* Still retransmitting, use backoff */
			tcp_set_rto(tp);
			tp->rto = tp->rto << tp->backoff;
		}
	} else {
		tcp_set_rto(tp);
	}

	tcp_bound_rto(tp);
}

static __inline__ void tcp_ack_packets_out(struct sock *sk, struct tcp_opt *tp)
{
	struct sk_buff *skb = skb_peek(&sk->write_queue);

	/* Some data was ACK'd, if still retransmitting (due to a
	 * timeout), resend more of the retransmit queue.  The
	 * congestion window is handled properly by that code.
	 */
	if (tp->retransmits) {
		tcp_xmit_retransmit_queue(sk);
		tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);
	} else {
		__u32 when = tp->rto - (tcp_time_stamp - TCP_SKB_CB(skb)->when);
		if ((__s32)when < 0)
			when = 1;
		tcp_reset_xmit_timer(sk, TIME_RETRANS, when);
	}
}

/* This routine deals with incoming acks, but not outgoing ones. */
static int tcp_ack(struct sock *sk, struct tcphdr *th, 
		   u32 ack_seq, u32 ack, int len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int flag = 0;
	u32 seq = 0;
	u32 seq_rtt = 0;

	if(sk->zapped)
		return(1);	/* Dead, can't ack any more so why bother */

	if (tp->pending == TIME_KEEPOPEN)
	  	tp->probes_out = 0;

	tp->rcv_tstamp = tcp_time_stamp;

	/* If the ack is newer than sent or older than previous acks
	 * then we can probably ignore it.
	 */
	if (after(ack, tp->snd_nxt) || before(ack, tp->snd_una))
		goto uninteresting_ack;

	/* If there is data set flag 1 */
	if (len != th->doff*4) {
		flag |= FLAG_DATA;
		tcp_delack_estimator(tp);
	}

	/* Update our send window. */

	/* This is the window update code as per RFC 793
	 * snd_wl{1,2} are used to prevent unordered
	 * segments from shrinking the window 
	 */
	if (before(tp->snd_wl1, ack_seq) ||
	    (tp->snd_wl1 == ack_seq && !after(tp->snd_wl2, ack))) {
		u32 nwin = ntohs(th->window) << tp->snd_wscale;

		if ((tp->snd_wl2 != ack) || (nwin > tp->snd_wnd)) {
			flag |= FLAG_WIN_UPDATE;
			tp->snd_wnd = nwin;

			tp->snd_wl1 = ack_seq;
			tp->snd_wl2 = ack;

			if (nwin > tp->max_window)
				tp->max_window = nwin;
		}
	}

	/* We passed data and got it acked, remove any soft error
	 * log. Something worked...
	 */
	sk->err_soft = 0;

	/* If this ack opens up a zero window, clear backoff.  It was
	 * being used to time the probes, and is probably far higher than
	 * it needs to be for normal retransmission.
	 */
	if (tp->pending == TIME_PROBE0)
		tcp_ack_probe(sk, ack);

	/* See if we can take anything off of the retransmit queue. */
	flag |= tcp_clean_rtx_queue(sk, ack, &seq, &seq_rtt);

	/* We must do this here, before code below clears out important
	 * state contained in tp->fackets_out and tp->retransmits.  -DaveM
	 */
	if (should_advance_cwnd(tp, flag))
		tcp_cong_avoid(tp);

	/* If we have a timestamp, we always do rtt estimates. */
	if (tp->saw_tstamp) {
		tcp_ack_saw_tstamp(sk, tp, seq, ack, flag);
	} else {
		/* If we were retransmiting don't count rtt estimate. */
		if (tp->retransmits) {
			if (tp->packets_out == 0) {
				tp->retransmits = 0;
				tp->fackets_out = 0;
				tp->retrans_out = 0;
			}
		} else {
			/* We don't have a timestamp. Can only use
			 * packets that are not retransmitted to determine
			 * rtt estimates. Also, we must not reset the
			 * backoff for rto until we get a non-retransmitted
			 * packet. This allows us to deal with a situation
			 * where the network delay has increased suddenly.
			 * I.e. Karn's algorithm. (SIGCOMM '87, p5.)
			 */
			if (flag & FLAG_DATA_ACKED) {
				if(!(flag & FLAG_RETRANS_DATA_ACKED)) {
					tp->backoff = 0;
					tcp_rtt_estimator(tp, seq_rtt);
					tcp_set_rto(tp);
					tcp_bound_rto(tp);
				}
			}
		}
	}

	if (tp->packets_out) {
		if (flag & FLAG_DATA_ACKED)
			tcp_ack_packets_out(sk, tp);
	} else {
		tcp_clear_xmit_timer(sk, TIME_RETRANS);
	}

	flag &= (FLAG_DATA | FLAG_WIN_UPDATE);
	if ((ack == tp->snd_una	&& tp->packets_out && flag == 0) ||
	    (tp->high_seq != 0)) {
		tcp_fast_retrans(sk, ack, flag);
	} else {
		/* Clear any aborted fast retransmit starts. */
		tp->dup_acks = 0;
	}
	/* It is not a brain fart, I thought a bit now. 8)
	 *
	 * Forward progress is indicated, if:
	 *   1. the ack acknowledges new data.
	 *   2. or the ack is duplicate, but it is caused by new segment
	 *      arrival. This case is filtered by:
	 *      - it contains no data, syn or fin.
	 *      - it does not update window.
	 *   3. or new SACK. It is difficult to check, so that we ignore it.
	 *
	 * Forward progress is also indicated by arrival new data,
	 * which was caused by window open from our side. This case is more
	 * difficult and it is made (alas, incorrectly) in tcp_data_queue().
	 *                                              --ANK (990513)
	 */
	if (ack != tp->snd_una || (flag == 0 && !th->fin))
		dst_confirm(sk->dst_cache);

	/* Remember the highest ack received. */
	tp->snd_una = ack;
	return 1;

uninteresting_ack:
	SOCK_DEBUG(sk, "Ack ignored %u %u\n", ack, tp->snd_nxt);
	return 0;
}

/* New-style handling of TIME_WAIT sockets. */
extern void tcp_tw_schedule(struct tcp_tw_bucket *tw);
extern void tcp_tw_reschedule(struct tcp_tw_bucket *tw);
extern void tcp_tw_deschedule(struct tcp_tw_bucket *tw);

void tcp_timewait_kill(struct tcp_tw_bucket *tw)
{
	struct tcp_bind_bucket *tb = tw->tb;

	/* Disassociate with bind bucket. */
	if(tw->bind_next)
		tw->bind_next->bind_pprev = tw->bind_pprev;
	*(tw->bind_pprev) = tw->bind_next;
	if (tb->owners == NULL) {
		if (tb->next)
			tb->next->pprev = tb->pprev;
		*(tb->pprev) = tb->next;
		kmem_cache_free(tcp_bucket_cachep, tb);
	}

	/* Unlink from established hashes. */
	if(tw->next)
		tw->next->pprev = tw->pprev;
	*tw->pprev = tw->next;

	/* We decremented the prot->inuse count when we entered TIME_WAIT
	 * and the sock from which this came was destroyed.
	 */
	tw->sklist_next->sklist_prev = tw->sklist_prev;
	tw->sklist_prev->sklist_next = tw->sklist_next;

	/* Ok, now free it up. */
	kmem_cache_free(tcp_timewait_cachep, tw);
}

/* We come here as a special case from the AF specific TCP input processing,
 * and the SKB has no owner.  Essentially handling this is very simple,
 * we just keep silently eating rx'd packets, acking them if necessary,
 * until none show up for the entire timeout period. 
 *
 * Return 0, TCP_TW_ACK, TCP_TW_RST
 */
enum tcp_tw_status 
tcp_timewait_state_process(struct tcp_tw_bucket *tw, struct sk_buff *skb,
			       struct tcphdr *th, unsigned len)
{
	/*	RFC 1122:
	 *	"When a connection is [...] on TIME-WAIT state [...]
	 *	[a TCP] MAY accept a new SYN from the remote TCP to
	 *	reopen the connection directly, if it:
	 *	
	 *	(1)  assigns its initial sequence number for the new
	 *	connection to be larger than the largest sequence
	 *	number it used on the previous connection incarnation,
	 *	and
	 *
	 *	(2)  returns to TIME-WAIT state if the SYN turns out 
	 *	to be an old duplicate".
	 */
	if(th->syn && !th->rst && after(TCP_SKB_CB(skb)->seq, tw->rcv_nxt)) {
		struct sock *sk;
		struct tcp_func *af_specific = tw->af_specific;
		__u32 isn;

		isn = tw->snd_nxt + 128000;
		if(isn == 0)
			isn++;
		tcp_tw_deschedule(tw);
		tcp_timewait_kill(tw);
		sk = af_specific->get_sock(skb, th);
		if(sk == NULL ||
		   !ipsec_sk_policy(sk,skb) ||
		   atomic_read(&sk->sock_readers) != 0)
			return 0;
		skb_set_owner_r(skb, sk);
		af_specific = sk->tp_pinfo.af_tcp.af_specific;
		if(af_specific->conn_request(sk, skb, isn) < 0)
			return TCP_TW_RST; /* Toss a reset back. */
		return 0; /* Discard the frame. */
	}

	/* Check RST or SYN */
	if(th->rst || th->syn) {
		/* This is TIME_WAIT assassination, in two flavors.
		 * Oh well... nobody has a sufficient solution to this
		 * protocol bug yet.
		 */
		if(sysctl_tcp_rfc1337 == 0) {
			tcp_tw_deschedule(tw);
			tcp_timewait_kill(tw);
		}
		if(!th->rst)
			return TCP_TW_RST; /* toss a reset back */
		return 0;
	} else {
		/* In this case we must reset the TIMEWAIT timer. */
		if(th->ack)
			tcp_tw_reschedule(tw);
	}
	/* Ack old packets if necessary */ 
	if (!after(TCP_SKB_CB(skb)->end_seq, tw->rcv_nxt) &&
	    (th->doff * 4) > len)
		return TCP_TW_ACK; 
	return 0; 
}

/* Enter the time wait state.  This is always called from BH
 * context.  Essentially we whip up a timewait bucket, copy the
 * relevant info into it from the SK, and mess with hash chains
 * and list linkage.
 */
static __inline__ void tcp_tw_hashdance(struct sock *sk, struct tcp_tw_bucket *tw)
{
	struct sock **head, *sktw;

	/* Step 1: Remove SK from established hash. */
	if(sk->next)
		sk->next->pprev = sk->pprev;
	*sk->pprev = sk->next;
	sk->pprev = NULL;
	tcp_reg_zap(sk);

	/* Step 2: Put TW into bind hash where SK was. */
	tw->tb = (struct tcp_bind_bucket *)sk->prev;
	if((tw->bind_next = sk->bind_next) != NULL)
		sk->bind_next->bind_pprev = &tw->bind_next;
	tw->bind_pprev = sk->bind_pprev;
	*sk->bind_pprev = (struct sock *)tw;
	sk->prev = NULL;

	/* Step 3: Same for the protocol sklist. */
	(tw->sklist_next = sk->sklist_next)->sklist_prev = (struct sock *)tw;
	(tw->sklist_prev = sk->sklist_prev)->sklist_next = (struct sock *)tw;
	sk->sklist_next = NULL;
	sk->prot->inuse--;

	/* Step 4: Hash TW into TIMEWAIT half of established hash table. */
	head = &tcp_ehash[sk->hashent + (tcp_ehash_size/2)];
	sktw = (struct sock *)tw;
	if((sktw->next = *head) != NULL)
		(*head)->pprev = &sktw->next;
	*head = sktw;
	sktw->pprev = head;
}

void tcp_time_wait(struct sock *sk)
{
	struct tcp_tw_bucket *tw;

	tw = kmem_cache_alloc(tcp_timewait_cachep, SLAB_ATOMIC);
	if(tw != NULL) {
		/* Give us an identity. */
		tw->daddr	= sk->daddr;
		tw->rcv_saddr	= sk->rcv_saddr;
		tw->bound_dev_if= sk->bound_dev_if;
		tw->num		= sk->num;
		tw->state	= TCP_TIME_WAIT;
		tw->sport	= sk->sport;
		tw->dport	= sk->dport;
		tw->family	= sk->family;
		tw->reuse	= sk->reuse;
		tw->rcv_nxt	= sk->tp_pinfo.af_tcp.rcv_nxt;
		tw->snd_nxt     = sk->tp_pinfo.af_tcp.snd_nxt;
		tw->window	= tcp_select_window(sk);
		tw->af_specific	= sk->tp_pinfo.af_tcp.af_specific;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		if(tw->family == PF_INET6) {
			memcpy(&tw->v6_daddr,
			       &sk->net_pinfo.af_inet6.daddr,
			       sizeof(struct in6_addr));
			memcpy(&tw->v6_rcv_saddr,
			       &sk->net_pinfo.af_inet6.rcv_saddr,
			       sizeof(struct in6_addr));
		}
#endif
		/* Linkage updates. */
		tcp_tw_hashdance(sk, tw);

		/* Get the TIME_WAIT timeout firing. */
		tcp_tw_schedule(tw);

		/* CLOSE the SK. */
		if(sk->state == TCP_ESTABLISHED)
			tcp_statistics.TcpCurrEstab--;
		sk->state = TCP_CLOSE;
		net_reset_timer(sk, TIME_DONE,
				min(sk->tp_pinfo.af_tcp.srtt * 2, TCP_DONE_TIME));
	} else {
		/* Sorry, we're out of memory, just CLOSE this
		 * socket up.  We've got bigger problems than
		 * non-graceful socket closings.
		 */
		tcp_set_state(sk, TCP_CLOSE);
	}

	/* Prevent rcvmsg/sndmsg calls, and wake people up. */
	sk->shutdown = SHUTDOWN_MASK;
	if(!sk->dead)
		sk->state_change(sk);
}

/*
 * 	Process the FIN bit. This now behaves as it is supposed to work
 *	and the FIN takes effect when it is validly part of sequence
 *	space. Not before when we get holes.
 *
 *	If we are ESTABLISHED, a received fin moves us to CLOSE-WAIT
 *	(and thence onto LAST-ACK and finally, CLOSE, we never enter
 *	TIME-WAIT)
 *
 *	If we are in FINWAIT-1, a received FIN indicates simultaneous
 *	close and we go into CLOSING (and later onto TIME-WAIT)
 *
 *	If we are in FINWAIT-2, a received FIN moves us to TIME-WAIT.
 */
 
static void tcp_fin(struct sk_buff *skb, struct sock *sk, struct tcphdr *th)
{
	sk->tp_pinfo.af_tcp.fin_seq = TCP_SKB_CB(skb)->end_seq;

	tcp_send_ack(sk);

	sk->shutdown |= RCV_SHUTDOWN;
	sk->done = 1;

	if (!sk->dead) {
		sk->state_change(sk);
		sock_wake_async(sk->socket, 1);
	}

	switch(sk->state) {
		case TCP_SYN_RECV:
		case TCP_ESTABLISHED:
			/* Move to CLOSE_WAIT */
			tcp_set_state(sk, TCP_CLOSE_WAIT);
			if (th->rst)
				sk->shutdown = SHUTDOWN_MASK;
			break;

		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
			/* Received a retransmission of the FIN, do
			 * nothing.
			 */
			break;
		case TCP_LAST_ACK:
			/* RFC793: Remain in the LAST-ACK state. */
			break;

		case TCP_FIN_WAIT1:
			/* This case occurs when a simultaneous close
			 * happens, we must ack the received FIN and
			 * enter the CLOSING state.
			 *
			 * This causes a WRITE timeout, which will either
			 * move on to TIME_WAIT when we timeout, or resend
			 * the FIN properly (maybe we get rid of that annoying
			 * FIN lost hang). The TIME_WRITE code is already 
			 * correct for handling this timeout.
			 */
			tcp_set_state(sk, TCP_CLOSING);
			break;
		case TCP_FIN_WAIT2:
			/* Received a FIN -- send ACK and enter TIME_WAIT. */
			tcp_time_wait(sk);
			break;
		default:
			/* Only TCP_LISTEN and TCP_CLOSE are left, in these
			 * cases we should never reach this piece of code.
			 */
			printk("tcp_fin: Impossible, sk->state=%d\n", sk->state);
			break;
	};
}

/* These routines update the SACK block as out-of-order packets arrive or
 * in-order packets close up the sequence space.
 */
static void tcp_sack_maybe_coalesce(struct tcp_opt *tp, struct tcp_sack_block *sp)
{
	int this_sack, num_sacks = tp->num_sacks;
	struct tcp_sack_block *swalk = &tp->selective_acks[0];

	/* If more than one SACK block, see if the recent change to SP eats into
	 * or hits the sequence space of other SACK blocks, if so coalesce.
	 */
	if(num_sacks != 1) {
		for(this_sack = 0; this_sack < num_sacks; this_sack++, swalk++) {
			if(swalk == sp)
				continue;

			/* First case, bottom of SP moves into top of the
			 * sequence space of SWALK.
			 */
			if(between(sp->start_seq, swalk->start_seq, swalk->end_seq)) {
				sp->start_seq = swalk->start_seq;
				goto coalesce;
			}
			/* Second case, top of SP moves into bottom of the
			 * sequence space of SWALK.
			 */
			if(between(sp->end_seq, swalk->start_seq, swalk->end_seq)) {
				sp->end_seq = swalk->end_seq;
				goto coalesce;
			}
		}
	}
	/* SP is the only SACK, or no coalescing cases found. */
	return;

coalesce:
	/* Zap SWALK, by moving every further SACK up by one slot.
	 * Decrease num_sacks.
	 */
	for(; this_sack < num_sacks-1; this_sack++, swalk++) {
		struct tcp_sack_block *next = (swalk + 1);
		swalk->start_seq = next->start_seq;
		swalk->end_seq = next->end_seq;
	}
	tp->num_sacks--;
}

static __inline__ void tcp_sack_swap(struct tcp_sack_block *sack1, struct tcp_sack_block *sack2)
{
	__u32 tmp;

	tmp = sack1->start_seq;
	sack1->start_seq = sack2->start_seq;
	sack2->start_seq = tmp;

	tmp = sack1->end_seq;
	sack1->end_seq = sack2->end_seq;
	sack2->end_seq = tmp;
}

static void tcp_sack_new_ofo_skb(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int cur_sacks = tp->num_sacks;

	if (!cur_sacks)
		goto new_sack;

	/* Optimize for the common case, new ofo frames arrive
	 * "in order". ;-)  This also satisfies the requirements
	 * of RFC2018 about ordering of SACKs.
	 */
	if(sp->end_seq == TCP_SKB_CB(skb)->seq) {
		sp->end_seq = TCP_SKB_CB(skb)->end_seq;
		tcp_sack_maybe_coalesce(tp, sp);
	} else if(sp->start_seq == TCP_SKB_CB(skb)->end_seq) {
		/* Re-ordered arrival, in this case, can be optimized
		 * as well.
		 */
		sp->start_seq = TCP_SKB_CB(skb)->seq;
		tcp_sack_maybe_coalesce(tp, sp);
	} else {
		struct tcp_sack_block *swap = sp + 1;
		int this_sack, max_sacks = (tp->tstamp_ok ? 3 : 4);

		/* Oh well, we have to move things around.
		 * Try to find a SACK we can tack this onto.
		 */

		for(this_sack = 1; this_sack < cur_sacks; this_sack++, swap++) {
			if((swap->end_seq == TCP_SKB_CB(skb)->seq) ||
			   (swap->start_seq == TCP_SKB_CB(skb)->end_seq)) {
				if(swap->end_seq == TCP_SKB_CB(skb)->seq)
					swap->end_seq = TCP_SKB_CB(skb)->end_seq;
				else
					swap->start_seq = TCP_SKB_CB(skb)->seq;
				tcp_sack_swap(sp, swap);
				tcp_sack_maybe_coalesce(tp, sp);
				return;
			}
		}

		/* Could not find an adjacent existing SACK, build a new one,
		 * put it at the front, and shift everyone else down.  We
		 * always know there is at least one SACK present already here.
		 *
		 * If the sack array is full, forget about the last one.
		 */
		if (cur_sacks >= max_sacks) {
			cur_sacks--;
			tp->num_sacks--;
		}
		while(cur_sacks >= 1) {
			struct tcp_sack_block *this = &tp->selective_acks[cur_sacks];
			struct tcp_sack_block *prev = (this - 1);
			this->start_seq = prev->start_seq;
			this->end_seq = prev->end_seq;
			cur_sacks--;
		}

	new_sack:
		/* Build the new head SACK, and we're done. */
		sp->start_seq = TCP_SKB_CB(skb)->seq;
		sp->end_seq = TCP_SKB_CB(skb)->end_seq;
		tp->num_sacks++;
	}
}

static void tcp_sack_remove_skb(struct tcp_opt *tp, struct sk_buff *skb)
{
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int num_sacks = tp->num_sacks;
	int this_sack;

	/* This is an in order data segment _or_ an out-of-order SKB being
	 * moved to the receive queue, so we know this removed SKB will eat
	 * from the front of a SACK.
	 */
	for(this_sack = 0; this_sack < num_sacks; this_sack++, sp++) {
		/* Check if the start of the sack is covered by skb. */
		if(!before(sp->start_seq, TCP_SKB_CB(skb)->seq) &&
		   before(sp->start_seq, TCP_SKB_CB(skb)->end_seq))
			break;
	}

	/* This should only happen if so many SACKs get built that some get
	 * pushed out before we get here, or we eat some in sequence packets
	 * which are before the first SACK block.
	 */
	if(this_sack >= num_sacks)
		return;

	sp->start_seq = TCP_SKB_CB(skb)->end_seq;
	if(!before(sp->start_seq, sp->end_seq)) {
		/* Zap this SACK, by moving forward any other SACKS. */
		for(this_sack += 1; this_sack < num_sacks; this_sack++, sp++) {
			struct tcp_sack_block *next = (sp + 1);
			sp->start_seq = next->start_seq;
			sp->end_seq = next->end_seq;
		}
		tp->num_sacks--;
	}
}

static void tcp_sack_extend(struct tcp_opt *tp, struct sk_buff *old_skb, struct sk_buff *new_skb)
{
	struct tcp_sack_block *sp = &tp->selective_acks[0];
	int num_sacks = tp->num_sacks;
	int this_sack;

	for(this_sack = 0; this_sack < num_sacks; this_sack++, sp++) {
		if(sp->end_seq == TCP_SKB_CB(old_skb)->end_seq)
			break;
	}
	if(this_sack >= num_sacks)
		return;
	sp->end_seq = TCP_SKB_CB(new_skb)->end_seq;
}

/* This one checks to see if we can put data from the
 * out_of_order queue into the receive_queue.
 */
static void tcp_ofo_queue(struct sock *sk)
{
	struct sk_buff *skb;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	while ((skb = skb_peek(&tp->out_of_order_queue))) {
		if (after(TCP_SKB_CB(skb)->seq, tp->rcv_nxt))
			break;

		if (!after(TCP_SKB_CB(skb)->end_seq, tp->rcv_nxt)) {
			SOCK_DEBUG(sk, "ofo packet was already received \n");
			__skb_unlink(skb, skb->list);
			kfree_skb(skb);
			continue;
		}
		SOCK_DEBUG(sk, "ofo requeuing : rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, TCP_SKB_CB(skb)->seq,
			   TCP_SKB_CB(skb)->end_seq);

		if(tp->sack_ok)
			tcp_sack_remove_skb(tp, skb);
		__skb_unlink(skb, skb->list);
		__skb_queue_tail(&sk->receive_queue, skb);
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if(skb->h.th->fin)
			tcp_fin(skb, sk, skb->h.th);
	}
}

static void tcp_data_queue(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skb1;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/*  Queue data for delivery to the user.
	 *  Packets in sequence go to the receive queue.
	 *  Out of sequence packets to the out_of_order_queue.
	 */
	if (TCP_SKB_CB(skb)->seq == tp->rcv_nxt) {
		/* Ok. In sequence. */
	queue_and_out:
		dst_confirm(sk->dst_cache);
		__skb_queue_tail(&sk->receive_queue, skb);
		tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if(skb->h.th->fin) {
			tcp_fin(skb, sk, skb->h.th);
		} else {
			tcp_remember_ack(tp, skb->h.th, skb); 
		}
		/* This may have eaten into a SACK block. */
		if(tp->sack_ok && tp->num_sacks)
			tcp_sack_remove_skb(tp, skb);
		tcp_ofo_queue(sk);

		/* Turn on fast path. */ 
		if (skb_queue_len(&tp->out_of_order_queue) == 0)
			tp->pred_flags = htonl(((tp->tcp_header_len >> 2) << 28) |
					       (0x10 << 16) |
					       tp->snd_wnd);
		return;
	}
	
	/* An old packet, either a retransmit or some packet got lost. */
	if (!after(TCP_SKB_CB(skb)->end_seq, tp->rcv_nxt)) {
		/* A retransmit, 2nd most common case.  Force an imediate ack. */
		SOCK_DEBUG(sk, "retransmit received: seq %X\n", TCP_SKB_CB(skb)->seq);
		tcp_enter_quickack_mode(tp);
		kfree_skb(skb);
		return;
	}

	if (before(TCP_SKB_CB(skb)->seq, tp->rcv_nxt)) {
		/* Partial packet, seq < rcv_next < end_seq */
		SOCK_DEBUG(sk, "partial packet: rcv_next %X seq %X - %X\n",
			   tp->rcv_nxt, TCP_SKB_CB(skb)->seq,
			   TCP_SKB_CB(skb)->end_seq);

		goto queue_and_out;
	}

	/* Ok. This is an out_of_order segment, force an ack. */
	tp->delayed_acks++;
	tcp_enter_quickack_mode(tp);

	/* Disable header prediction. */
	tp->pred_flags = 0;

	SOCK_DEBUG(sk, "out of order segment: rcv_next %X seq %X - %X\n",
		   tp->rcv_nxt, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq);

	if (skb_peek(&tp->out_of_order_queue) == NULL) {
		/* Initial out of order segment, build 1 SACK. */
		if(tp->sack_ok) {
			tp->num_sacks = 1;
			tp->selective_acks[0].start_seq = TCP_SKB_CB(skb)->seq;
			tp->selective_acks[0].end_seq = TCP_SKB_CB(skb)->end_seq;
		}
		__skb_queue_head(&tp->out_of_order_queue,skb);
	} else {
		for(skb1=tp->out_of_order_queue.prev; ; skb1 = skb1->prev) {
			/* Already there. */
			if (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb1)->seq) {
				if (skb->len >= skb1->len) {
					if(tp->sack_ok)
						tcp_sack_extend(tp, skb1, skb);
					__skb_append(skb1, skb);
					__skb_unlink(skb1, skb1->list);
					kfree_skb(skb1);
				} else {
					/* A duplicate, smaller than what is in the
					 * out-of-order queue right now, toss it.
					 */
					kfree_skb(skb);
				}
				break;
			}
			
			if (after(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb1)->seq)) {
				__skb_append(skb1, skb);
				if(tp->sack_ok)
					tcp_sack_new_ofo_skb(sk, skb);
				break;
			}

                        /* See if we've hit the start. If so insert. */
			if (skb1 == skb_peek(&tp->out_of_order_queue)) {
				__skb_queue_head(&tp->out_of_order_queue,skb);
				if(tp->sack_ok)
					tcp_sack_new_ofo_skb(sk, skb);
				break;
			}
		}
	}
}


/*
 *	This routine handles the data.  If there is room in the buffer,
 *	it will be have already been moved into it.  If there is no
 *	room, then we will just have to discard the packet.
 */

static int tcp_data(struct sk_buff *skb, struct sock *sk, unsigned int len)
{
	struct tcphdr *th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	th = skb->h.th;
	skb_pull(skb, th->doff*4);
	skb_trim(skb, len - (th->doff*4));

        if (skb->len == 0 && !th->fin)
		return(0);

	/* 
	 *	If our receive queue has grown past its limits shrink it.
	 *	Make sure to do this before moving snd_nxt, otherwise
	 *	data might be acked for that we don't have enough room.
	 */
	if (atomic_read(&sk->rmem_alloc) > sk->rcvbuf) { 
		if (prune_queue(sk) < 0) { 
			/* Still not enough room. That can happen when
			 * skb->true_size differs significantly from skb->len.
			 */
			return 0;
		}
	}

	tcp_data_queue(sk, skb);

	if (before(tp->rcv_nxt, tp->copied_seq)) {
		printk(KERN_DEBUG "*** tcp.c:tcp_data bug acked < copied\n");
		tp->rcv_nxt = tp->copied_seq;
	}

	/* Above, tcp_data_queue() increments delayed_acks appropriately.
	 * Now tell the user we may have some data.
	 */
	if (!sk->dead) {
		sk->data_ready(sk,0);
	}
	return(1);
}

static void __tcp_data_snd_check(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	if (!after(TCP_SKB_CB(skb)->end_seq, tp->snd_una + tp->snd_wnd) &&
	    tcp_packets_in_flight(tp) < tp->snd_cwnd) {
		/* Put more data onto the wire. */
		tcp_write_xmit(sk);
	} else if (tp->packets_out == 0 && !tp->pending) {
		/* Start probing the receivers window. */
		tcp_reset_xmit_timer(sk, TIME_PROBE0, tp->rto);
	}
}

static __inline__ void tcp_data_snd_check(struct sock *sk)
{
	struct sk_buff *skb = sk->tp_pinfo.af_tcp.send_head;

	if (skb != NULL)
		__tcp_data_snd_check(sk, skb); 
}

/* 
 * Adapt the MSS value used to make delayed ack decision to the 
 * real world. 
 */ 
static __inline__ void tcp_measure_rcv_mss(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	unsigned int len = skb->len, lss; 

	if (len > tp->rcv_mss) 
		tp->rcv_mss = len; 
	lss = tp->last_seg_size; 
	tp->last_seg_size = 0; 
	if (len >= 536) {
		if (len == lss) 
			tp->rcv_mss = len; 
		tp->last_seg_size = len; 
	}
}

/*
 * Check if sending an ack is needed.
 */
static __inline__ void __tcp_ack_snd_check(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* This also takes care of updating the window.
	 * This if statement needs to be simplified.
	 *
	 * Rules for delaying an ack:
	 *      - delay time <= 0.5 HZ
	 *      - we don't have a window update to send
	 *      - must send at least every 2 full sized packets
	 *	- must send an ACK if we have any out of order data
	 *
	 * With an extra heuristic to handle loss of packet
	 * situations and also helping the sender leave slow
	 * start in an expediant manner.
	 */

	    /* Two full frames received or... */
	if (((tp->rcv_nxt - tp->rcv_wup) >= tp->rcv_mss * MAX_DELAY_ACK) ||
	    /* We will update the window "significantly" or... */
	    tcp_raise_window(sk) ||
	    /* We entered "quick ACK" mode or... */
	    tcp_in_quickack_mode(tp) ||
	    /* We have out of order data */
	    (skb_peek(&tp->out_of_order_queue) != NULL)) {
		/* Then ack it now */
		tcp_send_ack(sk);
	} else {
		/* Else, send delayed ack. */
		tcp_send_delayed_ack(tp, HZ/2);
	}
}

static __inline__ void tcp_ack_snd_check(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	if (tp->delayed_acks == 0) {
		/* We sent a data segment already. */
		return;
	}
	__tcp_ack_snd_check(sk);
}


/*
 *	This routine is only called when we have urgent data
 *	signalled. Its the 'slow' part of tcp_urg. It could be
 *	moved inline now as tcp_urg is only called from one
 *	place. We handle URGent data wrong. We have to - as
 *	BSD still doesn't use the correction from RFC961.
 *	For 1003.1g we should support a new option TCP_STDURG to permit
 *	either form (or just set the sysctl tcp_stdurg).
 */
 
static void tcp_check_urg(struct sock * sk, struct tcphdr * th)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 ptr = ntohs(th->urg_ptr);

	if (ptr && !sysctl_tcp_stdurg)
		ptr--;
	ptr += ntohl(th->seq);

	/* Ignore urgent data that we've already seen and read. */
	if (after(tp->copied_seq, ptr))
		return;

	/* Do we already have a newer (or duplicate) urgent pointer? */
	if (tp->urg_data && !after(ptr, tp->urg_seq))
		return;

	/* Tell the world about our new urgent pointer. */
	if (sk->proc != 0) {
		if (sk->proc > 0)
			kill_proc(sk->proc, SIGURG, 1);
		else
			kill_pg(-sk->proc, SIGURG, 1);
	}

	/* We may be adding urgent data when the last byte read was
	 * urgent. To do this requires some care. We cannot just ignore
	 * tp->copied_seq since we would read the last urgent byte again
	 * as data, nor can we alter copied_seq until this data arrives
	 * or we break the sematics of SIOCATMARK (and thus sockatmark())
	 */
	if (tp->urg_seq == tp->copied_seq)
		tp->copied_seq++;	/* Move the copied sequence on correctly */
	tp->urg_data = URG_NOTYET;
	tp->urg_seq = ptr;

	/* Disable header prediction. */
	tp->pred_flags = 0;
}

/* This is the 'fast' part of urgent handling. */
static inline void tcp_urg(struct sock *sk, struct tcphdr *th, unsigned long len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Check if we get a new urgent pointer - normally not. */
	if (th->urg)
		tcp_check_urg(sk,th);

	/* Do we wait for any urgent data? - normally not... */
	if (tp->urg_data == URG_NOTYET) {
		u32 ptr = tp->urg_seq - ntohl(th->seq) + (th->doff*4);

		/* Is the urgent pointer pointing into this packet? */	 
		if (ptr < len) {
			tp->urg_data = URG_VALID | *(ptr + (unsigned char *) th);
			if (!sk->dead)
				sk->data_ready(sk,0);
		}
	}
}

/* Clean the out_of_order queue if we can, trying to get
 * the socket within its memory limits again.
 *
 * Return less than zero if we should start dropping frames
 * until the socket owning process reads some of the data
 * to stabilize the situation.
 */
static int prune_queue(struct sock *sk)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp; 
	struct sk_buff * skb;

	SOCK_DEBUG(sk, "prune_queue: c=%x\n", tp->copied_seq);

	net_statistics.PruneCalled++; 

	/* First, purge the out_of_order queue. */
	skb = __skb_dequeue_tail(&tp->out_of_order_queue);
	if(skb != NULL) {
		/* Free it all. */
		do {	net_statistics.OfoPruned += skb->len; 
			kfree_skb(skb);
			skb = __skb_dequeue_tail(&tp->out_of_order_queue);
		} while(skb != NULL);

		/* Reset SACK state.  A conforming SACK implementation will
		 * do the same at a timeout based retransmit.  When a connection
		 * is in a sad state like this, we care only about integrity
		 * of the connection not performance.
		 */
		if(tp->sack_ok)
			tp->num_sacks = 0;
	}
	
	/* If we are really being abused, tell the caller to silently
	 * drop receive data on the floor.  It will get retransmitted
	 * and hopefully then we'll have sufficient space.
	 *
	 * We used to try to purge the in-order packets too, but that
	 * turns out to be deadly and fraught with races.  Consider:
	 *
	 * 1) If we acked the data, we absolutely cannot drop the
	 *    packet.  This data would then never be retransmitted.
	 * 2) It is possible, with a proper sequence of events involving
	 *    delayed acks and backlog queue handling, to have the user
	 *    read the data before it gets acked.  The previous code
	 *    here got this wrong, and it lead to data corruption.
	 * 3) Too much state changes happen when the FIN arrives, so once
	 *    we've seen that we can't remove any in-order data safely.
	 *
	 * The net result is that removing in-order receive data is too
	 * complex for anyones sanity.  So we don't do it anymore.  But
	 * if we are really having our buffer space abused we stop accepting
	 * new receive data.
	 */
	if(atomic_read(&sk->rmem_alloc) < (sk->rcvbuf << 1))
		return 0;

	/* Massive buffer overcommit. */
	return -1;
}

/*
 *	TCP receive function for the ESTABLISHED state. 
 *
 *	It is split into a fast path and a slow path. The fast path is 
 * 	disabled when:
 *	- A zero window was announced from us - zero window probing
 *        is only handled properly in the slow path. 
 *      - Out of order segments arrived.
 *	- Urgent data is expected.
 *	- There is no buffer space left
 *	- Unexpected TCP flags/window values/header lengths are received
 *	  (detected by checking the TCP header against pred_flags) 
 *	- Data is sent in both directions. Fast path only supports pure senders
 *	  or pure receivers (this means either the sequence number or the ack
 *	  value must stay constant)
 *
 *	When these conditions are not satisfied it drops into a standard 
 *	receive procedure patterned after RFC793 to handle all cases.
 *	The first three cases are guaranteed by proper pred_flags setting,
 *	the rest is checked inline. Fast processing is turned on in 
 *	tcp_data_queue when everything is OK.
 */
int tcp_rcv_established(struct sock *sk, struct sk_buff *skb,
			struct tcphdr *th, unsigned len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int queued;
	u32 flg;

	/*
	 *	Header prediction.
	 *	The code follows the one in the famous 
	 *	"30 instruction TCP receive" Van Jacobson mail.
	 *	
	 *	Van's trick is to deposit buffers into socket queue 
	 *	on a device interrupt, to call tcp_recv function
	 *	on the receive process context and checksum and copy
	 *	the buffer to user space. smart...
	 *
	 *	Our current scheme is not silly either but we take the 
	 *	extra cost of the net_bh soft interrupt processing...
	 *	We do checksum and copy also but from device to kernel.
	 */

	/*
	 * RFC1323: H1. Apply PAWS check first.
	 */
	if (tcp_fast_parse_options(sk, th, tp)) {
		if (tp->saw_tstamp) {
			if (tcp_paws_discard(tp, th, len)) {
				tcp_statistics.TcpInErrs++;
				if (!th->rst) {
					tcp_send_ack(sk);
					goto discard;
				}
			}
			tcp_replace_ts_recent(sk, tp,
					      TCP_SKB_CB(skb)->seq,
					      TCP_SKB_CB(skb)->end_seq);
		}
	}

	flg = *(((u32 *)th) + 3) & ~htonl(0xFC8 << 16);

	/*	pred_flags is 0xS?10 << 16 + snd_wnd
	 *	if header_predition is to be made
	 *	'S' will always be tp->tcp_header_len >> 2
	 *	'?' will be 0 else it will be !0
	 *	(when there are holes in the receive 
	 *	 space for instance)
	 *	PSH flag is ignored.
         */

	if (flg == tp->pred_flags && TCP_SKB_CB(skb)->seq == tp->rcv_nxt) {
		if (len <= th->doff*4) {
			/* Bulk data transfer: sender */
			if (len == th->doff*4) {
				tcp_ack(sk, th, TCP_SKB_CB(skb)->seq,
					TCP_SKB_CB(skb)->ack_seq, len); 
				kfree_skb(skb); 
				tcp_data_snd_check(sk);
				return 0;
			} else { /* Header too small */
				tcp_statistics.TcpInErrs++;
				goto discard;
			}
		} else if (TCP_SKB_CB(skb)->ack_seq == tp->snd_una &&
			   atomic_read(&sk->rmem_alloc) <= sk->rcvbuf) {
			/* Bulk data transfer: receiver */
			__skb_pull(skb,th->doff*4);

			tcp_measure_rcv_mss(sk, skb); 

			/* DO NOT notify forward progress here.
			 * It saves dozen of CPU instructions in fast path. --ANK
			 */
			__skb_queue_tail(&sk->receive_queue, skb);
			tp->rcv_nxt = TCP_SKB_CB(skb)->end_seq;

			/* FIN bit check is not done since if FIN is set in
			 * this frame, the pred_flags won't match up. -DaveM
			 */
			sk->data_ready(sk, 0);
			tcp_delack_estimator(tp);

			tcp_remember_ack(tp, th, skb); 

			__tcp_ack_snd_check(sk);
			return 0;
		}
	}

	/*
	 *	Standard slow path.
	 */

	if (!tcp_sequence(tp, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq)) {
		/* RFC793, page 37: "In all states except SYN-SENT, all reset
		 * (RST) segments are validated by checking their SEQ-fields."
		 * And page 69: "If an incoming segment is not acceptable,
		 * an acknowledgment should be sent in reply (unless the RST bit
		 * is set, if so drop the segment and return)".
		 */
		if (th->rst)
			goto discard;
		if (after(TCP_SKB_CB(skb)->seq, tp->rcv_nxt)) {
			SOCK_DEBUG(sk, "seq:%d end:%d wup:%d wnd:%d\n",
				   TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
				   tp->rcv_wup, tp->rcv_wnd);
		}
		tcp_send_ack(sk);
		goto discard;
	}

	if(th->syn && TCP_SKB_CB(skb)->seq != tp->syn_seq) {
		SOCK_DEBUG(sk, "syn in established state\n");
		tcp_statistics.TcpInErrs++;
		tcp_reset(sk);
		return 1;
	}
	
	if(th->rst) {
		tcp_reset(sk);
		goto discard;
	}

	if(th->ack)
		tcp_ack(sk, th, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->ack_seq, len);
	
	/* Process urgent data. */
	tcp_urg(sk, th, len);

	/* step 7: process the segment text */
	queued = tcp_data(skb, sk, len);

	/* This must be after tcp_data() does the skb_pull() to
	 * remove the header size from skb->len.
	 *
	 * Dave!!! Phrase above (and all about rcv_mss) has 
	 * nothing to do with reality. rcv_mss must measure TOTAL
	 * size, including sacks, IP options etc. Hence, measure_rcv_mss
	 * must occur before pulling etc, otherwise it will flap
	 * like hell. Even putting it before tcp_data is wrong,
	 * it should use skb->tail - skb->nh.raw instead.
	 *					--ANK (980805)
	 * 
	 * BTW I broke it. Now all TCP options are handled equally
	 * in mss_clamp calculations (i.e. ignored, rfc1122),
	 * and mss_cache does include all of them (i.e. tstamps)
	 * except for sacks, to calculate effective mss faster.
	 * 					--ANK (980805)
	 */
	tcp_measure_rcv_mss(sk, skb); 

	/* Be careful, tcp_data() may have put this into TIME_WAIT. */
	if(sk->state != TCP_CLOSE) {
		tcp_data_snd_check(sk);
		tcp_ack_snd_check(sk);
	}

	if (!queued) {
	discard:
		kfree_skb(skb);
	}

	return 0;
}

/* 
 *	Process an incoming SYN or SYN-ACK for SYN_RECV sockets represented
 *	as an open_request. 
 */

struct sock *tcp_check_req(struct sock *sk, struct sk_buff *skb, 
			   struct open_request *req)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	u32 flg;

	/*	assumption: the socket is not in use.
	 *	as we checked the user count on tcp_rcv and we're
	 *	running from a soft interrupt.
	 */

	/* Check for syn retransmission */
	flg = *(((u32 *)skb->h.th) + 3);
	
	flg &= __constant_htonl(0x00170000);
	/* Only SYN set? */
	if (flg == __constant_htonl(0x00020000)) {
		if (TCP_SKB_CB(skb)->seq == req->rcv_isn) {
			/*	retransmited syn.
			 */
			req->class->rtx_syn_ack(sk, req); 
			return NULL;
		} else {
			return sk; /* Pass new SYN to the listen socket. */
		}
	}

	/* We know it's an ACK here */	
	if (req->sk) {
		/*	socket already created but not
		 *	yet accepted()...
		 */
		sk = req->sk;
	} else {
		/* In theory the packet could be for a cookie, but
		 * TIME_WAIT should guard us against this. 
		 * XXX: Nevertheless check for cookies?
		 * This sequence number check is done again later,
		 * but we do it here to prevent syn flood attackers
		 * from creating big SYN_RECV sockets.
		 */ 
		if (!between(TCP_SKB_CB(skb)->ack_seq, req->snt_isn, req->snt_isn+1) ||
		    !between(TCP_SKB_CB(skb)->seq, req->rcv_isn, 
			     req->rcv_isn+1+req->rcv_wnd)) {
			req->class->send_reset(skb);
			return NULL;
		}
	
		sk = tp->af_specific->syn_recv_sock(sk, skb, req, NULL);
		tcp_dec_slow_timer(TCP_SLT_SYNACK);
		if (sk == NULL)
			return NULL;
		
		req->expires = 0UL;
		req->sk = sk;
	}
	skb_orphan(skb); 
	skb_set_owner_r(skb, sk);
	return sk; 
}

/*
 *	This function implements the receiving procedure of RFC 793 for
 *	all states except ESTABLISHED and TIME_WAIT. 
 *	It's called from both tcp_v4_rcv and tcp_v6_rcv and should be
 *	address independent.
 */
	
int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb,
			  struct tcphdr *th, unsigned len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	int queued = 0;

	switch (sk->state) {
	case TCP_CLOSE:
		/* When state == CLOSED, hash lookup always fails.
		 *
		 * But, there is a back door, the backlog queue.
		 * If we have a sequence of packets in the backlog
		 * during __release_sock() which have a sequence such
		 * that:
		 *	packet X	causes entry to TCP_CLOSE state
		 *	...
		 *	packet X + N	has FIN bit set
		 *
		 * We report a (luckily) harmless error in this case.
		 * The issue is that backlog queue processing bypasses
		 * any hash lookups (we know which socket packets are for).
		 * The correct behavior here is what 2.0.x did, since
		 * a TCP_CLOSE socket does not exist.  Drop the frame
		 * and send a RST back to the other end.
		 */
		return 1;

	case TCP_LISTEN:
		/* These use the socket TOS.. 
		 * might want to be the received TOS 
		 */
		if(th->ack)
			return 1;
		
		if(th->syn) {
			if(tp->af_specific->conn_request(sk, skb, 0) < 0)
				return 1;

			/* Now we have several options: In theory there is 
			 * nothing else in the frame. KA9Q has an option to 
			 * send data with the syn, BSD accepts data with the
			 * syn up to the [to be] advertised window and 
			 * Solaris 2.1 gives you a protocol error. For now 
			 * we just ignore it, that fits the spec precisely 
			 * and avoids incompatibilities. It would be nice in
			 * future to drop through and process the data.
			 *
			 * Now that TTCP is starting to be used we ought to 
			 * queue this data.
			 * But, this leaves one open to an easy denial of
		 	 * service attack, and SYN cookies can't defend
			 * against this problem. So, we drop the data
			 * in the interest of security over speed.
			 */
			goto discard;
		}
		
		goto discard;
		break;

	case TCP_SYN_SENT:
		/* SYN sent means we have to look for a suitable ack and 
		 * either reset for bad matches or go to connected. 
		 * The SYN_SENT case is unusual and should
		 * not be in line code. [AC]
		 */
		if(th->ack) {
			/* rfc793:
			 * "If the state is SYN-SENT then
			 *    first check the ACK bit
			 *      If the ACK bit is set
			 *	  If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send
			 *        a reset (unless the RST bit is set, if so drop
			 *        the segment and return)"
			 *
			 *  I cite this place to emphasize one essential
			 *  detail, this check is different of one
			 *  in established state: SND.UNA <= SEG.ACK <= SND.NXT.
			 *  SEG_ACK == SND.UNA == ISS is invalid in SYN-SENT,
			 *  because we have no previous data sent before SYN.
			 *                                        --ANK(990513)
			 *
			 *  We do not send data with SYN, so that RFC-correct
			 *  test reduces to:
			 */
			if (sk->zapped ||
			    TCP_SKB_CB(skb)->ack_seq != tp->snd_nxt)
				return 1;

			/* Now ACK is acceptable.
			 *
			 * "If the RST bit is set
			 *    If the ACK was acceptable then signal the user "error:
			 *    connection reset", drop the segment, enter CLOSED state,
			 *    delete TCB, and return."
			 */

			if (th->rst) {
				tcp_reset(sk);
				goto discard;
			}

			/* rfc793:
			 *   "fifth, if neither of the SYN or RST bits is set then
			 *    drop the segment and return."
			 *
			 *    See note below!
			 *                                        --ANK(990513)
		         */
			
			if (!th->syn)
				goto discard;

			/* rfc793:
			 *   "If the SYN bit is on ...
			 *    are acceptable then ...
			 *    (our SYN has been ACKed), change the connection
			 *    state to ESTABLISHED..."
			 *
			 * Do you see? SYN-less ACKs in SYN-SENT state are
			 * completely ignored.
			 *
			 * The bug causing stalled SYN-SENT sockets
			 * was here: tcp_ack advanced snd_una and canceled
			 * retransmit timer, so that bare ACK received
			 * in SYN-SENT state (even with invalid ack==ISS,
			 * because tcp_ack check is too weak for SYN-SENT)
			 * causes moving socket to invalid semi-SYN-SENT,
			 * semi-ESTABLISHED state and connection hangs.
			 *
			 * There exist buggy stacks, which really send
			 * such ACKs: f.e. 202.226.91.94 (okigate.oki.co.jp)
			 * Actually, if this host did not try to get something
			 * from ftp.inr.ac.ru I'd never find this bug 8)
			 *
			 *                                     --ANK (990514)
			 */

			tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
			tcp_ack(sk,th, TCP_SKB_CB(skb)->seq,
				TCP_SKB_CB(skb)->ack_seq, len);

			/* Ok.. it's good. Set up sequence numbers and
			 * move to established.
			 */
			tp->rcv_nxt = TCP_SKB_CB(skb)->seq+1;
			tp->rcv_wup = TCP_SKB_CB(skb)->seq+1;

			/* RFC1323: The window in SYN & SYN/ACK segments is
			 * never scaled.
			 */
			tp->snd_wnd = htons(th->window);
			tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
			tp->snd_wl2 = TCP_SKB_CB(skb)->ack_seq;
			tp->fin_seq = TCP_SKB_CB(skb)->seq;

			tcp_set_state(sk, TCP_ESTABLISHED);
			tcp_parse_options(sk, th, tp, 0);

        		if (tp->wscale_ok == 0) {
                		tp->snd_wscale = tp->rcv_wscale = 0;
                		tp->window_clamp = min(tp->window_clamp,65535);
        		}

			if (tp->tstamp_ok) {
				tp->tcp_header_len =
					sizeof(struct tcphdr) + TCPOLEN_TSTAMP_ALIGNED;
			} else
				tp->tcp_header_len = sizeof(struct tcphdr);
			if (tp->saw_tstamp) {
				tp->ts_recent = tp->rcv_tsval;
				tp->ts_recent_stamp = tcp_time_stamp;
			}

			/* Can't be earlier, doff would be wrong. */
			tcp_send_ack(sk);

			sk->dport = th->source;
			tp->copied_seq = tp->rcv_nxt;

			if(!sk->dead) {
				sk->state_change(sk);
				sock_wake_async(sk->socket, 0);
			}
		} else {
			if(th->syn && !th->rst) {
				/* The previous version of the code
				 * checked for "connecting to self"
				 * here. that check is done now in
				 * tcp_connect.
				 */
				tcp_set_state(sk, TCP_SYN_RECV);
				tcp_parse_options(sk, th, tp, 0);
				if (tp->saw_tstamp) {
					tp->ts_recent = tp->rcv_tsval;
					tp->ts_recent_stamp = tcp_time_stamp;
				}
				
				tp->rcv_nxt = TCP_SKB_CB(skb)->seq + 1;
				tp->rcv_wup = TCP_SKB_CB(skb)->seq + 1;

				/* RFC1323: The window in SYN & SYN/ACK segments is
				 * never scaled.
				 */
				tp->snd_wnd = htons(th->window);
				tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
				
				tcp_send_synack(sk);
			} else
				break; 
		}

		/* tp->tcp_header_len and tp->mss_clamp
		   probably changed, synchronize mss.
		   */
		tcp_sync_mss(sk, tp->pmtu_cookie);
		tp->rcv_mss = tp->mss_cache;

		if (sk->state == TCP_SYN_RECV)
			goto discard;
		
		goto step6; 
	}

	/*   Parse the tcp_options present on this header.
	 *   By this point we really only expect timestamps.
	 *   Note that this really has to be here and not later for PAWS
	 *   (RFC1323) to work.
	 */
	if (tcp_fast_parse_options(sk, th, tp)) {
		/* NOTE: assumes saw_tstamp is never set if we didn't
		 * negotiate the option. tcp_fast_parse_options() must
		 * guarantee this.
		 */
		if (tp->saw_tstamp) {
			if (tcp_paws_discard(tp, th, len)) {
				tcp_statistics.TcpInErrs++;
				if (!th->rst) {
					tcp_send_ack(sk);
					goto discard;
				}
			}
			tcp_replace_ts_recent(sk, tp,
					      TCP_SKB_CB(skb)->seq,
					      TCP_SKB_CB(skb)->end_seq);
		}
	}

	/* The silly FIN test here is necessary to see an advancing ACK in
	 * retransmitted FIN frames properly.  Consider the following sequence:
	 *
	 *	host1 --> host2		FIN XSEQ:XSEQ(0) ack YSEQ
	 *	host2 --> host1		FIN YSEQ:YSEQ(0) ack XSEQ
	 *	host1 --> host2		XSEQ:XSEQ(0) ack YSEQ+1
	 *	host2 --> host1		FIN YSEQ:YSEQ(0) ack XSEQ+1	(fails tcp_sequence test)
	 *
	 * At this point the connection will deadlock with host1 believing
	 * that his FIN is never ACK'd, and thus it will retransmit it's FIN
	 * forever.  The following fix is from Taral (taral@taral.net).
	 */

	/* step 1: check sequence number */
	if (!tcp_sequence(tp, TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq) &&
	    !(th->fin && TCP_SKB_CB(skb)->end_seq == tp->rcv_nxt)) {
		if (!th->rst) {
			tcp_send_ack(sk);
		}
		goto discard;
	}

	/* step 2: check RST bit */
	if(th->rst) {
		tcp_reset(sk);
		goto discard;
	}

	/* step 3: check security and precedence [ignored] */

	/*	step 4:
	 *
	 *	Check for a SYN, and ensure it matches the SYN we were
	 *	first sent. We have to handle the rather unusual (but valid)
	 *	sequence that KA9Q derived products may generate of
	 *
	 *	SYN
	 *				SYN|ACK Data
	 *	ACK	(lost)
	 *				SYN|ACK Data + More Data
	 *	.. we must ACK not RST...
	 *
	 *	We keep syn_seq as the sequence space occupied by the 
	 *	original syn. 
	 */

	if (th->syn && TCP_SKB_CB(skb)->seq != tp->syn_seq) {
		tcp_reset(sk);
		return 1;
	}

	/* step 5: check the ACK field */
	if (th->ack) {
		int acceptable = tcp_ack(sk, th, TCP_SKB_CB(skb)->seq,
					 TCP_SKB_CB(skb)->ack_seq, len);
		
		switch(sk->state) {
		case TCP_SYN_RECV:
			if (acceptable) {
				tcp_set_state(sk, TCP_ESTABLISHED);
				sk->dport = th->source;
				tp->copied_seq = tp->rcv_nxt;

				if(!sk->dead)
					sk->state_change(sk);		

				tp->snd_una = TCP_SKB_CB(skb)->ack_seq;
				tp->snd_wnd = htons(th->window) << tp->snd_wscale;
				tp->snd_wl1 = TCP_SKB_CB(skb)->seq;
				tp->snd_wl2 = TCP_SKB_CB(skb)->ack_seq;

			} else {
				SOCK_DEBUG(sk, "bad ack\n");
				return 1;
			}
			break;

		case TCP_FIN_WAIT1:
			if (tp->snd_una == tp->write_seq) {
				sk->shutdown |= SEND_SHUTDOWN;
				tcp_set_state(sk, TCP_FIN_WAIT2);
				if (!sk->dead)
					sk->state_change(sk);
				else
					tcp_reset_msl_timer(sk, TIME_CLOSE, sysctl_tcp_fin_timeout);
			}
			break;

		case TCP_CLOSING:	
			if (tp->snd_una == tp->write_seq) {
				tcp_time_wait(sk);
				goto discard;
			}
			break;

		case TCP_LAST_ACK:
			if (tp->snd_una == tp->write_seq) {
				sk->shutdown = SHUTDOWN_MASK;
				tcp_set_state(sk,TCP_CLOSE);
				if (!sk->dead)
					sk->state_change(sk);
				goto discard;
			}
			break;
		}
	} else
		goto discard;

step6:
	/* step 6: check the URG bit */
	tcp_urg(sk, th, len);

	/* step 7: process the segment text */
	switch (sk->state) {
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
		if (!before(TCP_SKB_CB(skb)->seq, tp->fin_seq))
			break;
	
	case TCP_FIN_WAIT1:
	case TCP_FIN_WAIT2:
		/* RFC 793 says to queue data in these states,
		 * RFC 1122 says we MUST send a reset. 
		 * BSD 4.4 also does reset.
		 */
		if ((sk->shutdown & RCV_SHUTDOWN) && sk->dead) {
			if (after(TCP_SKB_CB(skb)->end_seq - th->fin, tp->rcv_nxt)) {
				tcp_reset(sk);
				return 1;
			}
		}
		
	case TCP_ESTABLISHED: 
		queued = tcp_data(skb, sk, len);

		/* This must be after tcp_data() does the skb_pull() to
		 * remove the header size from skb->len.
		 */
		tcp_measure_rcv_mss(sk, skb); 
		break;
	}

	tcp_data_snd_check(sk);
	tcp_ack_snd_check(sk);

	if (!queued) { 
discard:
		kfree_skb(skb);
	}
	return 0;
}
