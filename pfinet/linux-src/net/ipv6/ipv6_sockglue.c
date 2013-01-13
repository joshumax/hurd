/*
 *	IPv6 BSD socket options interface
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/net/ipv4/ip_sockglue.c
 *
 *	$Id: ipv6_sockglue.c,v 1.1 2007/10/08 21:12:30 stesie Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	FIXME: Make the setsockopt code POSIX compliant: That is
 *
 *	o	Return -EINVAL for setsockopt of short lengths
 *	o	Truncate getsockopt returns
 *	o	Return an optlen of the truncated length if need be
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/sysctl.h>

#include <net/sock.h>
#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <asm/uaccess.h>

struct ipv6_mib ipv6_statistics={0, };
struct packet_type ipv6_packet_type =
{
	__constant_htons(ETH_P_IPV6), 
	NULL,					/* All devices */
	ipv6_rcv,
	NULL,
	NULL
};

/*
 *	addrconf module should be notifyed of a device going up
 */
static struct notifier_block ipv6_dev_notf = {
	addrconf_notify,
	NULL,
	0
};

struct ip6_ra_chain *ip6_ra_chain;

int ip6_ra_control(struct sock *sk, int sel, void (*destructor)(struct sock *))
{
	struct ip6_ra_chain *ra, *new_ra, **rap;

	/* RA packet may be delivered ONLY to IPPROTO_RAW socket */
	if (sk->type != SOCK_RAW || sk->num != IPPROTO_RAW)
		return -EINVAL;

	new_ra = (sel>=0) ? kmalloc(sizeof(*new_ra), GFP_KERNEL) : NULL;

	for (rap = &ip6_ra_chain; (ra=*rap) != NULL; rap = &ra->next) {
		if (ra->sk == sk) {
			if (sel>=0) {
				if (new_ra)
					kfree(new_ra);
				return -EADDRINUSE;
			}

			*rap = ra->next;
			synchronize_bh();

			if (ra->destructor)
				ra->destructor(sk);
			kfree(ra);
			return 0;
		}
	}
	if (new_ra == NULL)
		return -ENOBUFS;
	new_ra->sk = sk;
	new_ra->sel = sel;
	new_ra->destructor = destructor;
	start_bh_atomic();
	new_ra->next = ra;
	*rap = new_ra;
	end_bh_atomic();
	return 0;
}


int ipv6_setsockopt(struct sock *sk, int level, int optname, char *optval, 
		    int optlen)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int val, valbool;
	int retv = -ENOPROTOOPT;

	if(level==SOL_IP && sk->type != SOCK_RAW)
		return udp_prot.setsockopt(sk, level, optname, optval, optlen);

	if(level!=SOL_IPV6)
		goto out;

	if (optval == NULL)
		val=0;
	else if (get_user(val, (int *) optval))
		return -EFAULT;

	valbool = (val!=0);

	switch (optname) {

	case IPV6_ADDRFORM:
		if (val == PF_INET) {
			struct ipv6_txoptions *opt;
			struct sk_buff *pktopt;

			if (sk->protocol != IPPROTO_UDP &&
			    sk->protocol != IPPROTO_TCP)
				goto out;

			lock_sock(sk);
			if (sk->state != TCP_ESTABLISHED) {
				retv = ENOTCONN;
				goto addrform_done;
			}

			if (ipv6_only_sock(sk) ||
			    !(ipv6_addr_type(&np->daddr) & IPV6_ADDR_MAPPED)) {
				retv = -EADDRNOTAVAIL;
				goto addrform_done;
			}

			fl6_free_socklist(sk);
			ipv6_sock_mc_close(sk);

			if (sk->protocol == IPPROTO_TCP) {
				struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
				
				sk->prot = &tcp_prot;
				tp->af_specific = &ipv4_specific;
				sk->socket->ops = &inet_stream_ops;
				sk->family = PF_INET;
				tcp_sync_mss(sk, tp->pmtu_cookie);
			} else {
				sk->prot = &udp_prot;
				sk->socket->ops = &inet_dgram_ops;
			}
			opt = xchg(&np->opt, NULL);
			if (opt)
				sock_kfree_s(sk, opt, opt->tot_len);
			pktopt = xchg(&np->pktoptions, NULL);
			if (pktopt)
				kfree_skb(pktopt);
			retv = 0;

addrform_done:
			release_sock(sk);
		} else {
			retv = -EINVAL;
		}
		break;

	case IPV6_V6ONLY:
		if (sk->num) {
			retv = -EINVAL;
			goto out;
		}
		np->ipv6only = valbool;
		retv = 0;
		break;

	case IPV6_PKTINFO:
		np->rxopt.bits.rxinfo = valbool;
		retv = 0;
		break;

	case IPV6_HOPLIMIT:
		np->rxopt.bits.rxhlim = valbool;
		retv = 0;
		break;

	case IPV6_RTHDR:
		retv = -EINVAL;
		if (val >= 0 && val <= 2) {
			np->rxopt.bits.srcrt = val;
			retv = 0;
		}
		break;

	case IPV6_HOPOPTS:
		np->rxopt.bits.hopopts = valbool;
		retv = 0;
		break;

	case IPV6_AUTHHDR:
		np->rxopt.bits.authhdr = valbool;
		retv = 0;
		break;

	case IPV6_DSTOPTS:
		np->rxopt.bits.dstopts = valbool;
		retv = 0;
		break;

	case IPV6_FLOWINFO:
		np->rxopt.bits.rxflow = valbool;
		return 0;

	case IPV6_PKTOPTIONS:
	{
		struct ipv6_txoptions *opt = NULL;
		struct msghdr msg;
		struct flowi fl;
		int junk;

		fl.fl6_flowlabel = 0;
		fl.oif = sk->bound_dev_if;

		if (optlen == 0)
			goto update;

		opt = sock_kmalloc(sk, sizeof(*opt) + optlen, GFP_KERNEL);
		retv = -ENOBUFS;
		if (opt == NULL)
			break;

		memset(opt, 0, sizeof(*opt));
		opt->tot_len = sizeof(*opt) + optlen;
		retv = -EFAULT;
		if (copy_from_user(opt+1, optval, optlen))
			goto done;

		msg.msg_controllen = optlen;
		msg.msg_control = (void*)(opt+1);

		retv = datagram_send_ctl(&msg, &fl, opt, &junk);
		if (retv)
			goto done;
update:
		retv = 0;
		start_bh_atomic();
		if (opt && sk->type == SOCK_STREAM) {
			struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
			if ((tcp_connected(sk->state) || sk->state == TCP_SYN_SENT)
			    && sk->daddr != LOOPBACK4_IPV6) {
				tp->ext_header_len = opt->opt_flen + opt->opt_nflen;
				tcp_sync_mss(sk, tp->pmtu_cookie);
			}
		}
		opt = xchg(&np->opt, opt);
		dst_release(xchg(&sk->dst_cache, NULL));
		end_bh_atomic();

done:
		if (opt)
			sock_kfree_s(sk, opt, opt->tot_len);
		break;
	}
	case IPV6_UNICAST_HOPS:
		if (val > 255 || val < -1)
			retv = -EINVAL;
		else {
			np->hop_limit = val;
			retv = 0;
		}
		break;

	case IPV6_MULTICAST_HOPS:
		if (val > 255 || val < -1)
			retv = -EINVAL;
		else {
			np->mcast_hops = val;
			retv = 0;
		}
		break;

	case IPV6_MULTICAST_LOOP:
		np->mc_loop = valbool;
		retv = 0;
		break;

	case IPV6_MULTICAST_IF:
		if (sk->bound_dev_if && sk->bound_dev_if != val) {
			retv = -EINVAL;
			break;
		}
		if (dev_get_by_index(val) == NULL) {
			retv = -ENODEV;
			break;
		}
		np->mcast_oif = val;
		retv = 0;
		break;
	case IPV6_ADD_MEMBERSHIP:
	case IPV6_DROP_MEMBERSHIP:
	{
		struct ipv6_mreq mreq;

		if (copy_from_user(&mreq, optval, sizeof(struct ipv6_mreq)))
			return -EFAULT;

		if (optname == IPV6_ADD_MEMBERSHIP)
			retv = ipv6_sock_mc_join(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
		else
			retv = ipv6_sock_mc_drop(sk, mreq.ipv6mr_ifindex, &mreq.ipv6mr_multiaddr);
		break;
	}
	case IPV6_ROUTER_ALERT:
		retv = ip6_ra_control(sk, val, NULL);
		break;
	case IPV6_MTU_DISCOVER:
		if (val<0 || val>2)
			return -EINVAL;
		np->pmtudisc = val;
		return 0;
	case IPV6_MTU:
		if (val && val < IPV6_MIN_MTU)
			return -EINVAL;
		np->frag_size = val;
		return 0;
	case IPV6_RECVERR:
		np->recverr = valbool;
		if (!val)
			skb_queue_purge(&sk->error_queue);
		return 0;
	case IPV6_FLOWINFO_SEND:
		np->sndflow = valbool;
		return 0;
	case IPV6_FLOWLABEL_MGR:
		return ipv6_flowlabel_opt(sk, optval, optlen);
	};

out:
	return retv;
}

int ipv6_getsockopt(struct sock *sk, int level, int optname, char *optval, 
		    int *optlen)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int len;
	int val;

	if(level==SOL_IP && sk->type != SOCK_RAW)
		return udp_prot.getsockopt(sk, level, optname, optval, optlen);
	if(level!=SOL_IPV6)
		return -ENOPROTOOPT;
	if (get_user(len, optlen))
		return -EFAULT;
	switch (optname) {
	case IPV6_PKTOPTIONS:
	{
		struct msghdr msg;
		struct sk_buff *skb;

		start_bh_atomic();
		skb = np->pktoptions;
		if (skb)
			atomic_inc(&skb->users);
		end_bh_atomic();

		if (skb) {
			int err;

			msg.msg_control = optval;
			msg.msg_controllen = len;
			msg.msg_flags = 0;
			err = datagram_recv_ctl(sk, &msg, skb);
			kfree_skb(skb);
			if (err)
				return err;
			len -= msg.msg_controllen;
		} else
			len = 0;
		return put_user(len, optlen);
	}
	case IP_MTU:
		val = 0;	
		lock_sock(sk);
		if (sk->dst_cache)		
			val = sk->dst_cache->pmtu;
		release_sock(sk);
		if (!val)
			return -ENOTCONN;
		break;
	case IPV6_V6ONLY:
		val = np->ipv6only;
		break;
	default:
		return -EINVAL;
	}
	len=min(sizeof(int),len);
	if(put_user(len, optlen))
		return -EFAULT;
	if(copy_to_user(optval,&val,len))
		return -EFAULT;
	return 0;
}

#if defined(MODULE) && defined(CONFIG_SYSCTL)

/*
 *	sysctl registration functions defined in sysctl_net_ipv6.c
 */

extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

__initfunc(void ipv6_packet_init(void))
{
	dev_add_pack(&ipv6_packet_type);
}

__initfunc(void ipv6_netdev_notif_init(void))
{
	register_netdevice_notifier(&ipv6_dev_notf);
}

#ifdef MODULE
void ipv6_packet_cleanup(void)
{
	dev_remove_pack(&ipv6_packet_type);
}

void ipv6_netdev_notif_cleanup(void)
{
	unregister_netdevice_notifier(&ipv6_dev_notf);
}
#endif
