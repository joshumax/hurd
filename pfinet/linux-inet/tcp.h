/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP module.
 *
 * Version:	@(#)tcp.h	1.0.5	05/23/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _TCP_H
#define _TCP_H

#include <linux/tcp.h>

#define MAX_SYN_SIZE	44 + MAX_HEADER
#define MAX_FIN_SIZE	40 + MAX_HEADER
#define MAX_ACK_SIZE	40 + MAX_HEADER
#define MAX_RESET_SIZE	40 + MAX_HEADER
#define MAX_WINDOW	16384
#define MIN_WINDOW	2048
#define MAX_ACK_BACKLOG	2
#define MIN_WRITE_SPACE	2048
#define TCP_WINDOW_DIFF	2048

/* urg_data states */
#define URG_VALID	0x0100
#define URG_NOTYET	0x0200
#define URG_READ	0x0400

#define TCP_RETR1	7	/*
				 * This is how many retries it does before it
				 * tries to figure out if the gateway is
				 * down.
				 */

#define TCP_RETR2	15	/*
				 * This should take at least
				 * 90 minutes to time out.
				 */

#define TCP_TIMEOUT_LEN	(15*60*HZ) /* should be about 15 mins		*/
#define TCP_TIMEWAIT_LEN (60*HZ) /* how long to wait to successfully 
				  * close the socket, about 60 seconds	*/
#define TCP_FIN_TIMEOUT (3*60*HZ) /* BSD style FIN_WAIT2 deadlock breaker */				  
#define TCP_ACK_TIME	(3*HZ)	/* time to delay before sending an ACK	*/
#define TCP_DONE_TIME	250	/* maximum time to wait before actually
				 * destroying a socket			*/
#define TCP_WRITE_TIME	3000	/* initial time to wait for an ACK,
			         * after last transmit			*/
#define TCP_TIMEOUT_INIT (3*HZ)	/* RFC 1122 initial timeout value	*/
#define TCP_SYN_RETRIES	5	/* number of times to retry opening a
				 * connection 				*/
#define TCP_PROBEWAIT_LEN 100	/* time to wait between probes when
				 * I've got something to write and
				 * there is no window			*/

#define TCP_NO_CHECK	0	/* turn to one if you want the default
				 * to be no checksum			*/


/*
 *	TCP option
 */
 
#define TCPOPT_NOP		1	/* Padding */
#define TCPOPT_EOL		0	/* End of options */
#define TCPOPT_MSS		2	/* Segment size negotiating */
/*
 *	We don't use these yet, but they are for PAWS and big windows
 */
#define TCPOPT_WINDOW		3	/* Window scaling */
#define TCPOPT_TIMESTAMP	8	/* Better RTT estimations/PAWS */


/*
 * The next routines deal with comparing 32 bit unsigned ints
 * and worry about wraparound (automatic with unsigned arithmetic).
 */

extern __inline int before(unsigned long seq1, unsigned long seq2)
{
        return (long)(seq1-seq2) < 0;
}

extern __inline int after(unsigned long seq1, unsigned long seq2)
{
	return (long)(seq1-seq2) > 0;
}


/* is s2<=s1<=s3 ? */
extern __inline int between(unsigned long seq1, unsigned long seq2, unsigned long seq3)
{
	return (after(seq1+1, seq2) && before(seq1, seq3+1));
}


/*
 * List all states of a TCP socket that can be viewed as a "connected"
 * state.  This now includes TCP_SYN_RECV, although I am not yet fully
 * convinced that this is the solution for the 'getpeername(2)'
 * problem. Thanks to Stephen A. Wood <saw@cebaf.gov>  -FvK
 */
extern __inline const int
tcp_connected(const int state)
{
  return(state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT ||
	 state == TCP_FIN_WAIT1   || state == TCP_FIN_WAIT2 ||
	 state == TCP_SYN_RECV);
}


extern struct proto tcp_prot;


extern void	tcp_err(int err, unsigned char *header, unsigned long daddr,
			unsigned long saddr, struct inet_protocol *protocol);
extern void	tcp_shutdown (struct sock *sk, int how);
extern int	tcp_rcv(struct sk_buff *skb, struct device *dev,
			struct options *opt, unsigned long daddr,
			unsigned short len, unsigned long saddr, int redo,
			struct inet_protocol *protocol);

extern int	tcp_ioctl(struct sock *sk, int cmd, unsigned long arg);

extern int tcp_select_window(struct sock *sk);
extern void tcp_send_check(struct tcphdr *th, unsigned long saddr, 
		unsigned long daddr, int len, struct sock *sk);
extern void tcp_send_probe0(struct sock *sk);
extern void tcp_enqueue_partial(struct sk_buff *, struct sock *);
extern struct sk_buff * tcp_dequeue_partial(struct sock *);


#endif	/* _TCP_H */
