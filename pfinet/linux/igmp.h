/*
 *	Linux NET3:	Internet Gateway Management Protocol  [IGMP]
 *
 *	Authors:
 *		Alan Cox <Alan.Cox@linux.org>	
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_IGMP_H
#define _LINUX_IGMP_H

/*
 *	IGMP protocol structures
 */

struct igmphdr
{
	unsigned char type;
	unsigned char unused;
	unsigned short csum;
	unsigned long group;
};

#define IGMP_HOST_MEMBERSHIP_QUERY	0x11	/* From RFC1112 */
#define IGMP_HOST_MEMBERSHIP_REPORT	0x12	/* Ditto */
#define IGMP_HOST_LEAVE_MESSAGE		0x17	/* An extra BSD seems to send */

				/* 224.0.0.1 */
#define IGMP_ALL_HOSTS		htonl(0xE0000001L)

/*
 * struct for keeping the multicast list in
 */

#ifdef __KERNEL__
struct ip_mc_socklist
{
	unsigned long multiaddr[IP_MAX_MEMBERSHIPS];	/* This is a speed trade off */
	struct device *multidev[IP_MAX_MEMBERSHIPS];
};

struct ip_mc_list 
{
	struct device *interface;
	unsigned long multiaddr;
	struct ip_mc_list *next;
	struct timer_list timer;
	int tm_running;
	int users;
};
 
extern struct ip_mc_list *ip_mc_head;


extern int igmp_rcv(struct sk_buff *, struct device *, struct options *, unsigned long, unsigned short,
	unsigned long, int , struct inet_protocol *);
extern void ip_mc_drop_device(struct device *dev); 
extern int ip_mc_join_group(struct sock *sk, struct device *dev, unsigned long addr);
extern int ip_mc_leave_group(struct sock *sk, struct device *dev,unsigned long addr);
extern void ip_mc_drop_socket(struct sock *sk);
#endif
#endif
