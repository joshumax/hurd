/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  NET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Ethernet handlers.
 *
 * Version:	@(#)eth.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ETH_H
#define _ETH_H


#include <linux/if_ether.h>


extern char		*eth_print(unsigned char *ptr);
extern void		eth_dump(struct ethhdr *eth);
extern int		eth_header(unsigned char *buff, struct device *dev,
				   unsigned short type, unsigned long daddr,
				   unsigned long saddr, unsigned len);
extern int		eth_rebuild_header(void *buff, struct device *dev);
extern void		eth_add_arp(unsigned long addr, struct sk_buff *skb,
				    struct device *dev);
extern unsigned short	eth_type_trans(struct sk_buff *skb, struct device *dev);

#endif	/* _ETH_H */
