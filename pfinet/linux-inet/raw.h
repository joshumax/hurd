/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the RAW-IP module.
 *
 * Version:	@(#)raw.h	1.0.2	05/07/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _RAW_H
#define _RAW_H


extern struct proto raw_prot;


extern void	raw_err(int err, unsigned char *header, unsigned long daddr,
			unsigned long saddr, struct inet_protocol *protocol);
extern int	raw_recvfrom(struct sock *sk, unsigned char *to,
			int len, int noblock, unsigned flags,
			struct sockaddr_in *sin, int *addr_len);
extern int	raw_read(struct sock *sk, unsigned char *buff,
			int len, int noblock, unsigned flags);
extern int 	raw_rcv(struct sock *, struct sk_buff *, struct device *, 
			long, long);

#endif	/* _RAW_H */
