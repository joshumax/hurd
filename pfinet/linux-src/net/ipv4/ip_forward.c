/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP forwarding functionality.
 *		
 * Version:	$Id: ip_forward.c,v 1.43.2.1 1999/11/16 06:33:43 davem Exp $
 *
 * Authors:	see ip.c
 *
 * Fixes:
 *		Joseph Gooch	:	Removed maddr selection for ip_masq, now done in ip_masq.c
 *		Many		:	Split from ip.c , see ip_input.c for 
 *					history.
 *		Dave Gregorich	:	NULL ip_rt_put fix for multicast 
 *					routing.
 *		Jos Vos		:	Add call_out_firewall before sending,
 *					use output device for accounting.
 *		Jos Vos		:	Call forward firewall after routing
 *					(always use output device).
 *		Mike McLagan	:	Routing by source
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#ifdef CONFIG_IP_MASQUERADE
#include <net/ip_masq.h>
#endif
#include <net/checksum.h>
#include <linux/route.h>
#include <net/route.h>

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check the packet against our socket administration to see
 *	if it is related to a connection on our system.
 *	Needed for transparent proxying.
 */

int ip_chksock(struct sk_buff *skb)
{
	switch (skb->nh.iph->protocol) {
	case IPPROTO_ICMP:
		return icmp_chkaddr(skb);
	case IPPROTO_TCP:
		return tcp_chkaddr(skb);
	case IPPROTO_UDP:
		return udp_chkaddr(skb);
	default:
		return 0;
	}
}
#endif


int ip_forward(struct sk_buff *skb)
{
	struct device *dev2;	/* Output device */
	struct iphdr *iph;	/* Our header */
	struct rtable *rt;	/* Route we use */
	struct ip_options * opt	= &(IPCB(skb)->opt);
	unsigned short mtu;
#if defined(CONFIG_FIREWALL) || defined(CONFIG_IP_MASQUERADE)
	int fw_res = 0;
#endif

	if (IPCB(skb)->opt.router_alert && ip_call_ra_chain(skb))
		return 0;

	if (skb->pkt_type != PACKET_HOST)
		goto drop;
	
	/*
	 *	According to the RFC, we must first decrease the TTL field. If
	 *	that reaches zero, we must reply an ICMP control message telling
	 *	that the packet's lifetime expired.
	 */

	iph = skb->nh.iph;
	rt = (struct rtable*)skb->dst;

#ifdef CONFIG_CPU_IS_SLOW
	if (net_cpu_congestion > 1 && !(iph->tos&IPTOS_RELIABILITY) &&
	    IPTOS_PREC(iph->tos) < IPTOS_PREC_INTERNETCONTROL) {
		if (((xtime.tv_usec&0xF)<<net_cpu_congestion) > 0x1C)
			goto drop;
	}
#endif


#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (ip_chksock(skb))
                goto local_pkt;
#endif

	if (iph->ttl <= 1)
                goto too_many_hops;

	if (opt->is_strictroute && rt->rt_dst != rt->rt_gateway)
                goto sr_failed;

	/*
	 *	Having picked a route we can now send the frame out
	 *	after asking the firewall permission to do so.
	 */

	skb->priority = rt_tos2priority(iph->tos);
	dev2 = rt->u.dst.dev;
	mtu = rt->u.dst.pmtu;

#ifdef CONFIG_NET_SECURITY
	call_fw_firewall(PF_SECURITY, dev2, NULL, &mtu, NULL);
#endif	
	
	/*
	 *	We now generate an ICMP HOST REDIRECT giving the route
	 *	we calculated.
	 */
	if (rt->rt_flags&RTCF_DOREDIRECT && !opt->srr)
		ip_rt_send_redirect(skb);

	/* We are about to mangle packet. Copy it! */
	if ((skb = skb_cow(skb, dev2->hard_header_len)) == NULL)
		return -1;
	iph = skb->nh.iph;
	opt = &(IPCB(skb)->opt);

	/* Decrease ttl after skb cow done */
	ip_decrease_ttl(iph);

	/*
	 * We now may allocate a new buffer, and copy the datagram into it.
	 * If the indicated interface is up and running, kick it.
	 */

	if (skb->len > mtu && (ntohs(iph->frag_off) & IP_DF))
		goto frag_needed;

#ifdef CONFIG_IP_ROUTE_NAT
	if (rt->rt_flags & RTCF_NAT) {
		if (ip_do_nat(skb)) {
			kfree_skb(skb);
			return -1;
		}
	}
#endif

#ifdef CONFIG_IP_MASQUERADE
	if(!(IPCB(skb)->flags&IPSKB_MASQUERADED)) {
		/* 
		 *	Check that any ICMP packets are not for a 
		 *	masqueraded connection.  If so rewrite them
		 *	and skip the firewall checks
		 */
		if (iph->protocol == IPPROTO_ICMP) {
#ifdef CONFIG_IP_MASQUERADE_ICMP
			struct icmphdr *icmph = (struct icmphdr *)((char*)iph + (iph->ihl << 2));
			if ((icmph->type==ICMP_DEST_UNREACH)||
			    (icmph->type==ICMP_SOURCE_QUENCH)||
			    (icmph->type==ICMP_TIME_EXCEEDED))
			{
#endif
				fw_res = ip_fw_masquerade(&skb, 0);
			        if (fw_res < 0) {
					kfree_skb(skb);
					return -1;
				}

				if (fw_res)
					/* ICMP matched - skip firewall */
					goto skip_call_fw_firewall;
#ifdef CONFIG_IP_MASQUERADE_ICMP
			}
#endif				
		}
		if (rt->rt_flags&RTCF_MASQ)
			goto skip_call_fw_firewall;
#endif /* CONFIG_IP_MASQUERADE */

#ifdef CONFIG_FIREWALL
		fw_res=call_fw_firewall(PF_INET, dev2, iph, NULL, &skb);
		switch (fw_res) {
		case FW_ACCEPT:
		case FW_MASQUERADE:
			break;
		case FW_REJECT:
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
			/* fall thru */
		default:
			kfree_skb(skb);
			return -1;
		}
#endif

#ifdef CONFIG_IP_MASQUERADE
	}

skip_call_fw_firewall:
	/*
	 * If this fragment needs masquerading, make it so...
	 * (Don't masquerade de-masqueraded fragments)
	 */
	if (!(IPCB(skb)->flags&IPSKB_MASQUERADED) &&
	    (fw_res==FW_MASQUERADE || rt->rt_flags&RTCF_MASQ)) {
		u32 maddr = 0;

#ifdef CONFIG_IP_ROUTE_NAT
		maddr = (rt->rt_flags&RTCF_MASQ) ? rt->rt_src_map : 0;
#endif
			if (ip_fw_masquerade(&skb, maddr) < 0) {
				kfree_skb(skb);
				return -1;
			} else {
				/*
				 *      Masquerader may have changed skb 
				 */
				iph = skb->nh.iph;
				opt = &(IPCB(skb)->opt);
			}
	}
#endif


#ifdef CONFIG_FIREWALL
	if ((fw_res = call_out_firewall(PF_INET, dev2, iph, NULL,&skb)) < FW_ACCEPT) {
		/* FW_ACCEPT and FW_MASQUERADE are treated equal:
		   masquerading is only supported via forward rules */
		if (fw_res == FW_REJECT)
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
		kfree_skb(skb);
		return -1;
	}
#endif

	ip_statistics.IpForwDatagrams++;

	if (opt->optlen == 0) {
#ifdef CONFIG_NET_FASTROUTE
		if (rt->rt_flags&RTCF_FAST && !netdev_fastroute_obstacles) {
			unsigned h = ((*(u8*)&rt->key.dst)^(*(u8*)&rt->key.src))&NETDEV_FASTROUTE_HMASK;
			/* Time to switch to functional programming :-) */
			dst_release_irqwait(xchg(&skb->dev->fastpath[h], dst_clone(&rt->u.dst)));
		}
#endif
		ip_send(skb);
		return 0;
	}

	ip_forward_options(skb);
	ip_send(skb);
	return 0;

#ifdef CONFIG_IP_TRANSPARENT_PROXY
local_pkt:
	return ip_local_deliver(skb);
#endif

frag_needed:
	ip_statistics.IpFragFails++;
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED, htonl(mtu));
        goto drop;

sr_failed:
        /*
	 *	Strict routing permits no gatewaying
	 */
         icmp_send(skb, ICMP_DEST_UNREACH, ICMP_SR_FAILED, 0);
         goto drop;

too_many_hops:
        /* Tell the sender its packet died... */
        icmp_send(skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, 0);
drop:
	kfree_skb(skb);
	return -1;
}
