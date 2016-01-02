/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP router.
 *
 * Version:	@(#)route.h	1.0.4	05/27/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 * Fixes:
 *		Alan Cox	:	Reformatted. Added ip_rt_local()
 *		Alan Cox	:	Support for TCP parameters.
 *		Alexey Kuznetsov:	Major changes for new routing code.
 *		Mike McLagan    :	Routing by source
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H

#include <linux/config.h>
#include <net/dst.h>
#include <linux/in_route.h>
#include <linux/rtnetlink.h>
#include <linux/route.h>

#ifndef __KERNEL__
#warning This file is not supposed to be used outside of kernel.
#endif

#define RT_HASH_DIVISOR	    	256

/*
 * Prevents LRU trashing, entries considered equivalent,
 * if the difference between last use times is less then this number.
 */
#define RT_CACHE_BUBBLE_THRESHOLD	(5*HZ)


#define RTO_ONLINK	0x01
#define RTO_TPROXY	0x80000000

#ifdef CONFIG_IP_TRANSPARENT_PROXY
#define RTO_CONN	RTO_TPROXY
#else
#define RTO_CONN	0
#endif

struct rt_key
{
	__u32			dst;
	__u32			src;
	int			iif;
	int			oif;
#ifdef CONFIG_IP_ROUTE_FWMARK
	__u32			fwmark;
#endif
	__u8			tos;
	__u8			scope;
};

struct rtable
{
	union
	{
		struct dst_entry	dst;
		struct rtable		*rt_next;
	} u;

	unsigned		rt_flags;
	unsigned		rt_type;

	__u32			rt_dst;	/* Path destination	*/
	__u32			rt_src;	/* Path source		*/
	int			rt_iif;

	/* Info on neighbour */
	__u32			rt_gateway;

	/* Cache lookup keys */
	struct rt_key		key;

	/* Miscellaneous cached information */
	__u32			rt_spec_dst; /* RFC1122 specific destination */

#ifdef CONFIG_IP_ROUTE_NAT
	__u32			rt_src_map;
	__u32			rt_dst_map;
#endif
};

extern struct rtable 	*rt_hash_table[RT_HASH_DIVISOR];

struct ip_rt_acct
{
	__u32 	o_bytes;
	__u32 	o_packets;
	__u32 	i_bytes;
	__u32 	i_packets;
};

extern struct ip_rt_acct ip_rt_acct[256];

extern void		ip_rt_init(void);
extern void		ip_rt_redirect(u32 old_gw, u32 dst, u32 new_gw,
				       u32 src, u8 tos, struct device *dev);
extern void		ip_rt_advice(struct rtable **rp, int advice);
extern void		rt_cache_flush(int how);
extern int		ip_route_output(struct rtable **, u32 dst, u32 src, u32 tos, int oif);
extern int		ip_route_input(struct sk_buff*, u32 dst, u32 src, u8 tos, struct device *devin);
extern unsigned short	ip_rt_frag_needed(struct iphdr *iph, unsigned short new_mtu);
extern void		ip_rt_update_pmtu(struct dst_entry *dst, unsigned mtu);
extern void		ip_rt_send_redirect(struct sk_buff *skb);

extern unsigned		inet_addr_type(u32 addr);
extern void		ip_rt_multicast_event(struct in_device *);
extern int		ip_rt_ioctl(unsigned int cmd, void *arg);
extern void		ip_rt_get_source(u8 *src, struct rtable *rt);
extern int		ip_rt_dump(struct sk_buff *skb,  struct netlink_callback *cb);


static __inline__ void ip_rt_put(struct rtable * rt)
{
	if (rt)
		dst_release(&rt->u.dst);
}

extern __u8 ip_tos2prio[16];

static __inline__ char rt_tos2priority(u8 tos)
{
	return ip_tos2prio[IPTOS_TOS(tos)>>1];
}

static __inline__ int ip_route_connect(struct rtable **rp, u32 dst, u32 src, u32 tos, int oif)
{
	int err;
	err = ip_route_output(rp, dst, src, tos, oif);
	if (err || (dst && src))
		return err;
	dst = (*rp)->rt_dst;
	src = (*rp)->rt_src;
	ip_rt_put(*rp);
	*rp = NULL;
	return ip_route_output(rp, dst, src, tos, oif);
}

#endif	/* _ROUTE_H */
