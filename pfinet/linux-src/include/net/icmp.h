/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ICMP module.
 *
 * Version:	@(#)icmp.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ICMP_H
#define	_ICMP_H

#include <linux/icmp.h>
#include <linux/skbuff.h>

#include <net/sock.h>
#include <net/protocol.h>

extern struct icmp_err icmp_err_convert[];
extern struct icmp_mib icmp_statistics;

extern void	icmp_send(struct sk_buff *skb_in,  int type, int code,
			  unsigned long info);
extern int	icmp_rcv(struct sk_buff *skb, unsigned short len);
extern int	icmp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern void	icmp_init(struct net_proto_family *ops);

/* Move into dst.h ? */
extern int 	xrlim_allow(struct dst_entry *dst, int timeout);

/* CONFIG_IP_TRANSPARENT_PROXY */
extern int	icmp_chkaddr(struct sk_buff *skb);

#endif	/* _ICMP_H */
