/*
 *	IPv6 input
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Ian P. Morris		<I.P.Morris@soton.ac.uk>
 *
 *	$Id: ip6_input.c,v 1.1 2007/10/08 21:12:30 stesie Exp $
 *
 *	Based in linux/net/ipv4/ip_input.c
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>


int ipv6_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct ipv6hdr *hdr;
	u32 		pkt_len;

	if (skb->pkt_type == PACKET_OTHERHOST)
		goto drop;

	ipv6_statistics.Ip6InReceives++;

	/* Store incoming device index. When the packet will
	   be queued, we cannot refer to skb->dev anymore.
	 */
	((struct inet6_skb_parm *)skb->cb)->iif = dev->ifindex;

	hdr = skb->nh.ipv6h;

	if (skb->len < sizeof(struct ipv6hdr) || hdr->version != 6)
		goto err;

	pkt_len = ntohs(hdr->payload_len);

	/* pkt_len may be zero if Jumbo payload option is present */
	if (pkt_len || hdr->nexthdr != NEXTHDR_HOP) {
		if (pkt_len + sizeof(struct ipv6hdr) > skb->len)
			goto truncated;
		skb_trim(skb, pkt_len + sizeof(struct ipv6hdr));
	}

	if (hdr->nexthdr == NEXTHDR_HOP) {
		skb->h.raw = (u8*)(hdr+1);
		if (!ipv6_parse_hopopts(skb, &hdr->nexthdr)) {
			ipv6_statistics.Ip6InHdrErrors++;
			return 0;
		}
	}

	if (skb->dst == NULL)
		ip6_route_input(skb);

	return skb->dst->input(skb);

truncated:
	ipv6_statistics.Ip6InTruncatedPkts++;
err:
	ipv6_statistics.Ip6InHdrErrors++;
drop:
	kfree_skb(skb);
	return 0;
}

/*
 *	0 - deliver
 *	1 - block
 */
static __inline__ int icmpv6_filter(struct sock *sk, struct sk_buff *skb)
{
	struct icmp6hdr *icmph;
	struct raw6_opt *opt;

	opt = &sk->tp_pinfo.tp_raw;
	icmph = (struct icmp6hdr *) (skb->nh.ipv6h + 1);
	return test_bit(icmph->icmp6_type, &opt->filter);
}

/*
 *	demultiplex raw sockets.
 *	(should consider queueing the skb in the sock receive_queue
 *	without calling rawv6.c)
 */
static struct sock * ipv6_raw_deliver(struct sk_buff *skb,
				      int nexthdr, unsigned long len)
{
	struct in6_addr *saddr;
	struct in6_addr *daddr;
	struct sock *sk, *sk2;
	__u8 hash;

	saddr = &skb->nh.ipv6h->saddr;
	daddr = saddr + 1;

	hash = nexthdr & (MAX_INET_PROTOS - 1);

	sk = raw_v6_htable[hash];

	/*
	 *	The first socket found will be delivered after
	 *	delivery to transport protocols.
	 */

	if (sk == NULL)
		return NULL;

	sk = raw_v6_lookup(sk, nexthdr, daddr, saddr);

	if (sk) {
		sk2 = sk;

		while ((sk2 = raw_v6_lookup(sk2->next, nexthdr, daddr, saddr))) {
			struct sk_buff *buff;

			if (nexthdr == IPPROTO_ICMPV6 &&
			    icmpv6_filter(sk2, skb))
				continue;

			buff = skb_clone(skb, GFP_ATOMIC);
			if (buff)
				rawv6_rcv(sk2, buff, len);
		}
	}

	if (sk && nexthdr == IPPROTO_ICMPV6 && icmpv6_filter(sk, skb))
		sk = NULL;

	return sk;
}

/*
 *	Deliver the packet to the host
 */

int ip6_input(struct sk_buff *skb)
{
	struct ipv6hdr *hdr = skb->nh.ipv6h;
	struct inet6_protocol *ipprot;
	struct sock *raw_sk;
	__u8 *nhptr;
	int nexthdr;
	int found = 0;
	u8 hash;
	int len;
	
	skb->h.raw = skb->nh.raw + sizeof(struct ipv6hdr);

	/*
	 *	Parse extension headers
	 */

	nexthdr = hdr->nexthdr;
	nhptr = &hdr->nexthdr;

	/* Skip  hop-by-hop options, they are already parsed. */
	if (nexthdr == NEXTHDR_HOP) {
		nhptr = (u8*)(hdr+1);
		nexthdr = *nhptr;
		skb->h.raw += (nhptr[1]+1)<<3;
	}

	/* This check is sort of optimization.
	   It would be stupid to detect for optional headers,
	   which are missing with probability of 200%
	 */
	if (nexthdr != IPPROTO_TCP && nexthdr != IPPROTO_UDP) {
		nhptr = ipv6_parse_exthdrs(&skb, nhptr);
		if (nhptr == NULL)
			return 0;
		nexthdr = *nhptr;
		hdr = skb->nh.ipv6h;
	}
	len = skb->tail - skb->h.raw;

	raw_sk = ipv6_raw_deliver(skb, nexthdr, len);

	hash = nexthdr & (MAX_INET_PROTOS - 1);
	for (ipprot = (struct inet6_protocol *) inet6_protos[hash]; 
	     ipprot != NULL; 
	     ipprot = (struct inet6_protocol *) ipprot->next) {
		struct sk_buff *buff = skb;

		if (ipprot->protocol != nexthdr)
			continue;

		if (ipprot->copy || raw_sk)
			buff = skb_clone(skb, GFP_ATOMIC);
		/* buff == NULL ?????? */
		ipprot->handler(buff, len);
		found = 1;
	}

	if (raw_sk) {
		rawv6_rcv(raw_sk, skb, len);
		found = 1;
	}

	/*
	 *	not found: send ICMP parameter problem back
	 */
	if (!found) {
		ipv6_statistics.Ip6InUnknownProtos++;
		icmpv6_param_prob(skb, ICMPV6_UNK_NEXTHDR, nhptr);
	}

	return 0;
}

int ip6_mc_input(struct sk_buff *skb)
{
	struct ipv6hdr *hdr;	
	int deliver = 0;
	int discard = 1;

	ipv6_statistics.Ip6InMcastPkts++;

	hdr = skb->nh.ipv6h;
	if (ipv6_chk_mcast_addr(skb->dev, &hdr->daddr))
		deliver = 1;

	/*
	 *	IPv6 multicast router mode isn't currently supported.
	 */
#if 0
	if (ipv6_config.multicast_route) {
		int addr_type;

		addr_type = ipv6_addr_type(&hdr->daddr);

		if (!(addr_type & (IPV6_ADDR_LOOPBACK | IPV6_ADDR_LINKLOCAL))) {
			struct sk_buff *skb2;
			struct dst_entry *dst;

			dst = skb->dst;
			
			if (deliver) {
				skb2 = skb_clone(skb, GFP_ATOMIC);
			} else {
				discard = 0;
				skb2 = skb;
			}

			dst->output(skb2);
		}
	}
#endif

	if (deliver) {
		discard = 0;
		ip6_input(skb);
	}

	if (discard)
		kfree_skb(skb);

	return 0;
}
