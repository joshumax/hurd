/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the AF_INET socket handler.
 *
 * Version:	@(#)sock.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche <flla@stud.uni-sb.de>
 *
 * Fixes:
 *		Alan Cox	:	Volatiles in skbuff pointers. See
 *					skbuff comments. May be overdone,
 *					better to prove they can be removed
 *					than the reverse.
 *		Alan Cox	:	Added a zapped field for tcp to note
 *					a socket is reset and must stay shut up
 *		Alan Cox	:	New fields for options
 *	Pauline Middelink	:	identd support
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _SOCK_H
#define _SOCK_H

#include <linux/timer.h>
#include <linux/ip.h>		/* struct options */
#include <linux/tcp.h>		/* struct tcphdr */
#include <linux/config.h>

#include <linux/skbuff.h>	/* struct sk_buff */
#include "protocol.h"		/* struct inet_protocol */
#ifdef CONFIG_AX25
#include "ax25.h"
#endif
#ifdef CONFIG_IPX
#include "ipx.h"
#endif
#ifdef CONFIG_ATALK
#include <linux/atalk.h>
#endif

#include <linux/igmp.h>

#define SOCK_ARRAY_SIZE	256		/* Think big (also on some systems a byte is faster */


/*
 * This structure really needs to be cleaned up.
 * Most of it is for TCP, and not used by any of
 * the other protocols.
 */
struct sock {
  struct options		*opt;
  volatile unsigned long	wmem_alloc;
  volatile unsigned long	rmem_alloc;
  unsigned long			write_seq;
  unsigned long			sent_seq;
  unsigned long			acked_seq;
  unsigned long			copied_seq;
  unsigned long			rcv_ack_seq;
  unsigned long			window_seq;
  unsigned long			fin_seq;
  unsigned long			urg_seq;
  unsigned long			urg_data;

  /*
   * Not all are volatile, but some are, so we
   * might as well say they all are.
   */
  volatile char                 inuse,
				dead,
				urginline,
				intr,
				blog,
				done,
				reuse,
				keepopen,
				linger,
				delay_acks,
				destroy,
				ack_timed,
				no_check,
				zapped,	/* In ax25 & ipx means not linked */
				broadcast,
				nonagle;
  unsigned long		        lingertime;
  int				proc;
  struct sock			*next;
  struct sock			*prev; /* Doubly linked chain.. */
  struct sock			*pair;
  struct sk_buff		* volatile send_head;
  struct sk_buff		* volatile send_tail;
  struct sk_buff_head		back_log;
  struct sk_buff		*partial;
  struct timer_list		partial_timer;
  long				retransmits;
  struct sk_buff_head		write_queue,
				receive_queue;
  struct proto			*prot;
  struct wait_queue		**sleep;
  unsigned long			daddr;
  unsigned long			saddr;
  unsigned short		max_unacked;
  unsigned short		window;
  unsigned short		bytes_rcv;
/* mss is min(mtu, max_window) */
  unsigned short		mtu;       /* mss negotiated in the syn's */
  volatile unsigned short	mss;       /* current eff. mss - can change */
  volatile unsigned short	user_mss;  /* mss requested by user in ioctl */
  volatile unsigned short	max_window;
  unsigned long 		window_clamp;
  unsigned short		num;
  volatile unsigned short	cong_window;
  volatile unsigned short	cong_count;
  volatile unsigned short	ssthresh;
  volatile unsigned short	packets_out;
  volatile unsigned short	shutdown;
  volatile unsigned long	rtt;
  volatile unsigned long	mdev;
  volatile unsigned long	rto;
/* currently backoff isn't used, but I'm maintaining it in case
 * we want to go back to a backoff formula that needs it
 */
  volatile unsigned short	backoff;
  volatile short		err;
  unsigned char			protocol;
  volatile unsigned char	state;
  volatile unsigned char	ack_backlog;
  unsigned char			max_ack_backlog;
  unsigned char			priority;
  unsigned char			debug;
  unsigned short		rcvbuf;
  unsigned short		sndbuf;
  unsigned short		type;
  unsigned char			localroute;	/* Route locally only */
#ifdef CONFIG_IPX
  ipx_address			ipx_dest_addr;
  ipx_interface			*ipx_intrfc;
  unsigned short		ipx_port;
  unsigned short		ipx_type;
#endif
#ifdef CONFIG_AX25
/* Really we want to add a per protocol private area */
  ax25_address			ax25_source_addr,ax25_dest_addr;
  struct sk_buff *volatile	ax25_retxq[8];
  char				ax25_state,ax25_vs,ax25_vr,ax25_lastrxnr,ax25_lasttxnr;
  char				ax25_condition;
  char				ax25_retxcnt;
  char				ax25_xx;
  char				ax25_retxqi;
  char				ax25_rrtimer;
  char				ax25_timer;
  unsigned char			ax25_n2;
  unsigned short		ax25_t1,ax25_t2,ax25_t3;
  ax25_digi			*ax25_digipeat;
#endif  
#ifdef CONFIG_ATALK
  struct atalk_sock		at;
#endif

/* IP 'private area' or will be eventually */
  int				ip_ttl;		/* TTL setting */
  int				ip_tos;		/* TOS */
  struct tcphdr			dummy_th;
  struct timer_list		keepalive_timer;	/* TCP keepalive hack */
  struct timer_list		retransmit_timer;	/* TCP retransmit timer */
  struct timer_list		ack_timer;		/* TCP delayed ack timer */
  int				ip_xmit_timeout;	/* Why the timeout is running */
#ifdef CONFIG_IP_MULTICAST  
  int				ip_mc_ttl;			/* Multicasting TTL */
  int				ip_mc_loop;			/* Loopback (not implemented yet) */
  char				ip_mc_name[MAX_ADDR_LEN];	/* Multicast device name */
  struct ip_mc_socklist		*ip_mc_list;			/* Group array */
#endif  

  /* This part is used for the timeout functions (timer.c). */
  int				timeout;	/* What are we waiting for? */
  struct timer_list		timer;		/* This is the TIME_WAIT/receive timer when we are doing IP */
  struct timeval		stamp;

  /* identd */
  struct socket			*socket;
  
  /* Callbacks */
  void				(*state_change)(struct sock *sk);
  void				(*data_ready)(struct sock *sk,int bytes);
  void				(*write_space)(struct sock *sk);
  void				(*error_report)(struct sock *sk);
  
};

struct proto {
  struct sk_buff *	(*wmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  struct sk_buff *	(*rmalloc)(struct sock *sk,
				    unsigned long size, int force,
				    int priority);
  void			(*wfree)(struct sock *sk, struct sk_buff *skb,
				 unsigned long size);
  void			(*rfree)(struct sock *sk, struct sk_buff *skb,
				 unsigned long size);
  unsigned long		(*rspace)(struct sock *sk);
  unsigned long		(*wspace)(struct sock *sk);
  void			(*close)(struct sock *sk, int timeout);
  int			(*read)(struct sock *sk, unsigned char *to,
				int len, int nonblock, unsigned flags);
  int			(*write)(struct sock *sk, unsigned char *to,
				 int len, int nonblock, unsigned flags);
  int			(*sendto)(struct sock *sk,
				  unsigned char *from, int len, int noblock,
				  unsigned flags, struct sockaddr_in *usin,
				  int addr_len);
  int			(*recvfrom)(struct sock *sk,
				    unsigned char *from, int len, int noblock,
				    unsigned flags, struct sockaddr_in *usin,
				    int *addr_len);
  int			(*build_header)(struct sk_buff *skb,
					unsigned long saddr,
					unsigned long daddr,
					struct device **dev, int type,
					struct options *opt, int len, int tos, int ttl);
  int			(*connect)(struct sock *sk,
				  struct sockaddr_in *usin, int addr_len);
  struct sock *		(*accept) (struct sock *sk, int flags);
  void			(*queue_xmit)(struct sock *sk,
				      struct device *dev, struct sk_buff *skb,
				      int free);
  void			(*retransmit)(struct sock *sk, int all);
  void			(*write_wakeup)(struct sock *sk);
  void			(*read_wakeup)(struct sock *sk);
  int			(*rcv)(struct sk_buff *buff, struct device *dev,
			       struct options *opt, unsigned long daddr,
			       unsigned short len, unsigned long saddr,
			       int redo, struct inet_protocol *protocol);
  int			(*select)(struct sock *sk, int which,
				  select_table *wait);
  int			(*ioctl)(struct sock *sk, int cmd,
				 unsigned long arg);
  int			(*init)(struct sock *sk);
  void			(*shutdown)(struct sock *sk, int how);
  int			(*setsockopt)(struct sock *sk, int level, int optname,
  				 char *optval, int optlen);
  int			(*getsockopt)(struct sock *sk, int level, int optname,
  				char *optval, int *option);  	 
  unsigned short	max_header;
  unsigned long		retransmits;
  struct sock *		sock_array[SOCK_ARRAY_SIZE];
  char			name[80];
  int			inuse, highestinuse;
};

#define TIME_WRITE	1
#define TIME_CLOSE	2
#define TIME_KEEPOPEN	3
#define TIME_DESTROY	4
#define TIME_DONE	5	/* used to absorb those last few packets */
#define TIME_PROBE0	6
#define SOCK_DESTROY_TIME 1000	/* about 10 seconds			*/

#define PROT_SOCK	1024	/* Sockets 0-1023 can't be bound too unless you are superuser */

#define SHUTDOWN_MASK	3
#define RCV_SHUTDOWN	1
#define SEND_SHUTDOWN	2


extern void			destroy_sock(struct sock *sk);
extern unsigned short		get_new_socknum(struct proto *, unsigned short);
extern void			put_sock(unsigned short, struct sock *); 
extern void			release_sock(struct sock *sk);
extern struct sock		*get_sock(struct proto *, unsigned short,
					  unsigned long, unsigned short,
					  unsigned long);
extern struct sock		*get_sock_mcast(struct sock *, unsigned short,
					  unsigned long, unsigned short,
					  unsigned long);
extern struct sock		*get_sock_raw(struct sock *, unsigned short,
					  unsigned long, unsigned long);

extern struct sk_buff		*sock_wmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern struct sk_buff		*sock_rmalloc(struct sock *sk,
					      unsigned long size, int force,
					      int priority);
extern void			sock_wfree(struct sock *sk, struct sk_buff *skb,
					   unsigned long size);
extern void			sock_rfree(struct sock *sk, struct sk_buff *skb,
					   unsigned long size);
extern unsigned long		sock_rspace(struct sock *sk);
extern unsigned long		sock_wspace(struct sock *sk);

extern int			sock_setsockopt(struct sock *sk,int level,int op,char *optval,int optlen);

extern int			sock_getsockopt(struct sock *sk,int level,int op,char *optval,int *optlen);
extern struct sk_buff 		*sock_alloc_send_skb(struct sock *skb, unsigned long size, int noblock, int *errcode);
extern int			sock_queue_rcv_skb(struct sock *sk, struct sk_buff *skb);

/* declarations from timer.c */
extern struct sock *timer_base;

void delete_timer (struct sock *);
void reset_timer (struct sock *, int, unsigned long);
void net_timer (unsigned long);


#endif	/* _SOCK_H */
