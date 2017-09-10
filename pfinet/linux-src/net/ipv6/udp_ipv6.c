/*
 *	UDP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *
 *	Based on linux/ipv4/udp.c
 *
 *	$Id: udp_ipv6.c,v 1.3 2007/10/13 01:43:00 stesie Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>

#include <net/checksum.h>

struct udp_mib udp_stats_in6;

static __inline__ int udv6_rcv_saddr_equal(struct sock *sk, struct sock *sk2)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int addr_type = ipv6_addr_type(&np->rcv_saddr);

	if (!sk2->rcv_saddr && !ipv6_only_sock(sk))
		return 1;

	if (sk2->family == AF_INET6 &&
	    ipv6_addr_any(&sk2->rcv_saddr) &&
	    !(ipv6_only_sock(sk2) && addr_type == IPV6_ADDR_MAPPED))
		return 1;

	if (addr_type == IPV6_ADDR_ANY &&
	    (!ipv6_only_sock(sk) ||
	     !(sk2->family == AF_INET6 ?
	       ipv6_addr_type(&sk2->rcv_saddr) == IPV6_ADDR_MAPPED : 1)))
		return 1;

	if (sk2->family == AF_INET6 &&
	    !ipv6_addr_cmp(&sk->rcv_saddr,
			   &sk2->rcv_saddr))
		return 1;

	if (addr_type == IPV6_ADDR_MAPPED &&
	    !ipv6_only_sock(sk2) &&
	    (!sk2->rcv_saddr ||
	     !sk->rcv_saddr ||
	     sk->rcv_saddr == sk2->rcv_saddr))
		return 1;

	return 0;
}


/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 */
static int udp_v6_get_port(struct sock *sk, unsigned short snum)
{
	SOCKHASH_LOCK();
	if (snum == 0) {
		int low = sysctl_local_port_range[0];
		int high = sysctl_local_port_range[1];
		int best_size_so_far, best, result, i;

		best_size_so_far = 32767;
		best = result = net_random() % (high - low) + low;
		for (i = 0; i < UDP_HTABLE_SIZE; i++, result++) {
			struct sock *sk;
			int size;

			sk = udp_hash[result & (UDP_HTABLE_SIZE - 1)];
			if (!sk) {
				if (result > sysctl_local_port_range[1])
					result = sysctl_local_port_range[0] +
						((result - sysctl_local_port_range[0]) &
						 (UDP_HTABLE_SIZE - 1));
				goto gotit;
			}
			size = 0;
			do {
				if (++size >= best_size_so_far)
					goto next;
			} while ((sk = sk->next) != NULL);
			best_size_so_far = size;
			best = result;

		next:
			(void) 0;
		}
		result = best;
		for(;; result += UDP_HTABLE_SIZE) {
			if (result > sysctl_local_port_range[1])
				result = sysctl_local_port_range[0]
					+ ((result - sysctl_local_port_range[0]) &
					   (UDP_HTABLE_SIZE - 1));
			if (!udp_lport_inuse(result))
				break;
		}
gotit:
		snum = result;
	} else {
		struct sock *sk2;
		int addr_type = ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr);

		for (sk2 = udp_hash[snum & (UDP_HTABLE_SIZE - 1)];
		     sk2 != NULL;
		     sk2 = sk2->next) {
			if (sk2->num == snum &&
			    sk2 != sk &&
			    sk2->bound_dev_if == sk->bound_dev_if &&
			    (!sk2->reuse || !sk->reuse) &&
			    udv6_rcv_saddr_equal(sk, sk2))
				goto fail;
		}
	}

	sk->num = snum;
	SOCKHASH_UNLOCK();
	return 0;

fail:
	SOCKHASH_UNLOCK();
	return 1;
}

static void udp_v6_hash(struct sock *sk)
{
	struct sock **skp = &udp_hash[sk->num & (UDP_HTABLE_SIZE - 1)];

	SOCKHASH_LOCK();
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	SOCKHASH_UNLOCK();
}

static void udp_v6_unhash(struct sock *sk)
{
	SOCKHASH_LOCK();
	if (sk->pprev) {
		if (sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
	}
	SOCKHASH_UNLOCK();
}

static struct sock *udp_v6_lookup(struct in6_addr *saddr, u16 sport,
				  struct in6_addr *daddr, u16 dport, int dif)
{
	struct sock *sk, *result = NULL;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

	for(sk = udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]; sk != NULL; sk = sk->next) {
		if((sk->num == hnum)		&&
		   (sk->family == PF_INET6)	&&
		   !(sk->dead && (sk->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
			int score = 0;
			if(sk->dport) {
				if(sk->dport != sport)
					continue;
				score++;
			}
			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(ipv6_addr_cmp(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if(!ipv6_addr_any(&np->daddr)) {
				if(ipv6_addr_cmp(&np->daddr, saddr))
					continue;
				score++;
			}
			if(sk->bound_dev_if) {
				if(sk->bound_dev_if != dif)
					continue;
				score++;
			}
			if(score == 4) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	return result;
}

/*
 *
 */

int udpv6_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct ipv6_pinfo      	*np = &sk->net_pinfo.af_inet6;
	struct in6_addr		*daddr;
	struct in6_addr		saddr;
	struct dst_entry	*dst;
	struct flowi		fl;
	struct ip6_flowlabel	*flowlabel = NULL;
	int			addr_type;
	int			err;

	if (usin->sin6_family == AF_INET) {
		if (__ipv6_only_sock(sk))
			return -EAFNOSUPPORT;
		err = udp_connect(sk, uaddr, addr_len);
		goto ipv4_connected;
	}

	if (usin->sin6_family == AF_UNSPEC) {
		udp_connect(sk, uaddr, addr_len);
		ipv6_addr_set(&np->daddr, 0, 0, 0, 0);
		ipv6_addr_set(&np->saddr, 0, 0, 0, 0);
		ipv6_addr_set(&np->rcv_saddr, 0, 0, 0, 0);
		return 0;
	}

	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	if (usin->sin6_family && usin->sin6_family != AF_INET6) 
	  	return(-EAFNOSUPPORT);

	fl.fl6_flowlabel = 0;
	if (np->sndflow) {
		fl.fl6_flowlabel = usin->sin6_flowinfo&IPV6_FLOWINFO_MASK;
		if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
			ipv6_addr_copy(&usin->sin6_addr, &flowlabel->dst);
		}
	}

	addr_type = ipv6_addr_type(&usin->sin6_addr);

	if (addr_type == IPV6_ADDR_ANY) {
		/*
		 *	connect to self
		 */
		usin->sin6_addr.s6_addr[15] = 0x01;
	}

	daddr = &usin->sin6_addr;

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;

		if (__ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];
		sin.sin_port = usin->sin6_port;

		err = udp_connect(sk, (struct sockaddr*) &sin, sizeof(sin));

ipv4_connected:
		if (err < 0)
			return err;
		
		ipv6_addr_set(&np->daddr, 0, 0, 
			      __constant_htonl(0x0000ffff),
			      sk->daddr);

		if(ipv6_addr_any(&np->saddr)) {
			ipv6_addr_set(&np->saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->saddr);

		}

		if(ipv6_addr_any(&np->rcv_saddr)) {
			ipv6_addr_set(&np->rcv_saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->rcv_saddr);
		}
		return 0;
	}

	if (addr_type&IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    usin->sin6_scope_id) {
			if (sk->bound_dev_if &&
			    sk->bound_dev_if != usin->sin6_scope_id)
				return(-EINVAL);

			sk->bound_dev_if = usin->sin6_scope_id;
			if (!sk->bound_dev_if &&
			    (addr_type & IPV6_ADDR_MULTICAST))
				fl.oif = np->mcast_oif;
		}

		/* Connect to link-local address requires an interface */
		if (!sk->bound_dev_if)
			return(-EINVAL);
	}

	ipv6_addr_copy(&np->daddr, daddr);
	np->flow_label = fl.fl6_flowlabel;

	sk->dport = usin->sin6_port;

	/*
	 *	Check for a route to destination an obtain the
	 *	destination cache for it.
	 */

	fl.proto = IPPROTO_UDP;
	fl.fl6_dst = &np->daddr;
	fl.fl6_src = &saddr;
	fl.oif = sk->bound_dev_if;
	fl.uli_u.ports.dport = sk->dport;
	fl.uli_u.ports.sport = sk->sport;

	if (flowlabel) {
		if (flowlabel->opt && flowlabel->opt->srcrt) {
			struct rt0_hdr *rt0 = (struct rt0_hdr *) flowlabel->opt->srcrt;
			fl.fl6_dst = rt0->addr;
		}
	} else if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
		fl.fl6_dst = rt0->addr;
	}

	dst = ip6_route_output(sk, &fl);

	if ((err = dst->error) != 0) {
		dst_release(dst);
		fl6_sock_release(flowlabel);
		return err;
	}

	ip6_dst_store(sk, dst, fl.fl6_dst);

	/* get the source adddress used in the appropriate device */

	err = ipv6_get_saddr(dst, daddr, &saddr);

	if (err == 0) {
		if(ipv6_addr_any(&np->saddr))
			ipv6_addr_copy(&np->saddr, &saddr);

		if(ipv6_addr_any(&np->rcv_saddr)) {
			ipv6_addr_copy(&np->rcv_saddr, &saddr);
			sk->rcv_saddr = 0xffffffff;
		}
		sk->state = TCP_ESTABLISHED;
	}
	fl6_sock_release(flowlabel);

	return err;
}

static void udpv6_close(struct sock *sk, long timeout)
{
	/* See for explanation: raw_close in ipv4/raw.c */
	sk->state = TCP_CLOSE;
	udp_v6_unhash(sk);
	sk->dead = 1;
	destroy_sock(sk);
}

#ifndef HAVE_CSUM_COPY_USER
#undef CONFIG_UDP_DELAY_CSUM
#endif

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags, int *addr_len)
{
  	struct sk_buff *skb;
  	int copied, err;

  	if (addr_len)
  		*addr_len=sizeof(struct sockaddr_in6);
  
	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

 	copied = skb->len - sizeof(struct udphdr);
  	if (copied > len) {
  		copied = len;
  		msg->msg_flags |= MSG_TRUNC;
  	}

#ifndef CONFIG_UDP_DELAY_CSUM
	err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), 
				      msg->msg_iov, copied);
#else
	if (skb->ip_summed==CHECKSUM_UNNECESSARY) {
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else if (copied > msg->msg_iov[0].iov_len || (msg->msg_flags&MSG_TRUNC)) {
		if ((unsigned short)csum_fold(csum_partial(skb->h.raw, skb->len, skb->csum))) {
			/* Error for blocking case is chosen to masquerade
			   as some normal condition.
			 */
			err = (flags&MSG_DONTWAIT) ? -EAGAIN : -EHOSTUNREACH;
			udp_stats_in6.UdpInErrors++;
			goto out_free;
		}
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else {
		unsigned int csum = csum_partial(skb->h.raw, sizeof(struct udphdr), skb->csum);

		err = 0;
		csum = csum_and_copy_to_user((char*)&skb->h.uh[1], msg->msg_iov[0].iov_base, copied, csum, &err);
		if (err)
			goto out_free;
		if ((unsigned short)csum_fold(csum)) {
			/* Error for blocking case is chosen to masquerade
			   as some normal condition.
			 */
			err = (flags&MSG_DONTWAIT) ? -EAGAIN : -EHOSTUNREACH;
			udp_stats_in6.UdpInErrors++;
			goto out_free;
		}
	}
#endif
	if (err)
		goto out_free;

	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (msg->msg_name) {
		struct sockaddr_in6 *sin6;
	  
		sin6 = (struct sockaddr_in6 *) msg->msg_name;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = skb->h.uh->source;
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;

		if (skb->protocol == __constant_htons(ETH_P_IP)) {
			ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				      __constant_htonl(0xffff), skb->nh.iph->saddr);
			if (sk->ip_cmsg_flags)
				ip_cmsg_recv(msg, skb);
		} else {
			memcpy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr,
			       sizeof(struct in6_addr));
			if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
				sin6->sin6_scope_id =
					((struct inet6_skb_parm *) skb->cb)->iif;

			if (sk->net_pinfo.af_inet6.rxopt.all)
				datagram_recv_ctl(sk, msg, skb);
		}
  	}
	err = copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;
}

void udpv6_err(struct sk_buff *skb, struct ipv6hdr *hdr,
	       struct inet6_skb_parm *opt,
	       int type, int code, unsigned char *buff, __u32 info)
{
	struct device *dev = skb->dev;
	struct in6_addr *saddr = &hdr->saddr;
	struct in6_addr *daddr = &hdr->daddr;
	struct sock *sk;
	struct udphdr *uh;
	int err;

	if (buff + sizeof(struct udphdr) > skb->tail)
		return;

	uh = (struct udphdr *) buff;

	sk = udp_v6_lookup(daddr, uh->dest, saddr, uh->source, dev->ifindex);
   
	if (sk == NULL)
		return;

	if (!icmpv6_err_convert(type, code, &err) &&
	    !sk->net_pinfo.af_inet6.recverr)
		return;

	if (sk->bsdism && sk->state!=TCP_ESTABLISHED)
		return;

	if (sk->net_pinfo.af_inet6.recverr)
		ipv6_icmp_error(sk, skb, err, uh->dest, ntohl(info), (u8 *)(uh+1));

	sk->err = err;
	sk->error_report(sk);
}

static inline int udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
#if defined(CONFIG_FILTER) && defined(CONFIG_UDP_DELAY_CSUM)
	if (sk->filter && skb->ip_summed != CHECKSUM_UNNECESSARY) {
		if ((unsigned short)csum_fold(csum_partial(skb->h.raw, skb->len, skb->csum))) {
			udp_stats_in6.UdpInErrors++;
			ipv6_statistics.Ip6InDiscards++;
			kfree_skb(skb);
			return 0;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
#endif
	if (sock_queue_rcv_skb(sk,skb)<0) {
		udp_stats_in6.UdpInErrors++;
		ipv6_statistics.Ip6InDiscards++;
		kfree_skb(skb);
		return 0;
	}
  	ipv6_statistics.Ip6InDelivers++;
	udp_stats_in6.UdpInDatagrams++;
	return 0;
}

static __inline__ int inet6_mc_check(struct sock *sk, struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc;
		
	for (mc = sk->net_pinfo.af_inet6.ipv6_mc_list; mc; mc=mc->next) {
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
			return 1;
	}

	return 0;
}

static struct sock *udp_v6_mcast_next(struct sock *sk,
				      u16 loc_port, struct in6_addr *loc_addr,
				      u16 rmt_port, struct in6_addr *rmt_addr,
				      int dif)
{
	struct sock *s = sk;
	unsigned short num = ntohs(loc_port);
	for(; s; s = s->next) {
		if((s->num == num)		&&
		   !(s->dead && (s->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &s->net_pinfo.af_inet6;
			if(s->dport) {
				if(s->dport != rmt_port)
					continue;
			}
			if(!ipv6_addr_any(&np->daddr) &&
			   ipv6_addr_cmp(&np->daddr, rmt_addr))
				continue;

			if (s->bound_dev_if && s->bound_dev_if != dif)
				continue;

			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
					return s;
			}
			if(!inet6_mc_check(s, loc_addr))
				continue;
			return s;
		}
	}
	return NULL;
}

/*
 * Note: called only from the BH handler context,
 * so we don't need to lock the hashes.
 */
static void udpv6_mcast_deliver(struct udphdr *uh,
				struct in6_addr *saddr, struct in6_addr *daddr,
				struct sk_buff *skb)
{
	struct sock *sk, *sk2;
	struct sk_buff *buff;
	int dif;

	sk = udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)];
	dif = skb->dev->ifindex;
	sk = udp_v6_mcast_next(sk, uh->dest, daddr, uh->source, saddr, dif);
	if (!sk)
		goto free_skb;

	buff = NULL;
	sk2 = sk;
	while((sk2 = udp_v6_mcast_next(sk2->next, uh->dest, saddr,
						  uh->source, daddr, dif))) {
		if (!buff) {
			buff = skb_clone(skb, GFP_ATOMIC);
			if (!buff)
				continue;
		}
		if (sock_queue_rcv_skb(sk2, buff) >= 0)
			buff = NULL;
	}
	if (buff)
		kfree_skb(buff);
	if (sock_queue_rcv_skb(sk, skb) < 0) {
free_skb:
		kfree_skb(skb);
	}
}

int udpv6_rcv(struct sk_buff *skb, unsigned long len)
{
	struct sock *sk;
  	struct udphdr *uh;
	struct device *dev = skb->dev;
	struct in6_addr *saddr = &skb->nh.ipv6h->saddr;
	struct in6_addr *daddr = &skb->nh.ipv6h->daddr;
	u32 ulen;

	uh = skb->h.uh;
	__skb_pull(skb, skb->h.raw - skb->data);

	ulen = ntohs(uh->len);

	/* Check for jumbo payload */
	if (ulen == 0 && skb->nh.ipv6h->payload_len == 0)
		ulen = len;

	if (ulen > len || len < sizeof(*uh)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "UDP: short packet: %d/%ld\n", ulen, len);
		udp_stats_in6.UdpInErrors++;
		kfree_skb(skb);
		return(0);
	}

	if (uh->check == 0) {
		/* IPv6 draft-v2 section 8.1 says that we SHOULD log
		   this error. Well, it is reasonable.
		 */
		if (net_ratelimit())
			printk(KERN_INFO "IPv6: udp checksum is 0\n");
		goto discard;
	}

	skb_trim(skb, ulen);

#ifndef CONFIG_UDP_DELAY_CSUM
	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char*)uh, ulen, 0);
	case CHECKSUM_HW:
		if (csum_ipv6_magic(saddr, daddr, ulen, IPPROTO_UDP, skb->csum)) {
			printk(KERN_DEBUG "IPv6: udp checksum error\n");
			goto discard;
		}
	};
#else
	if (skb->ip_summed==CHECKSUM_HW) {
		if (csum_ipv6_magic(saddr, daddr, ulen, IPPROTO_UDP, skb->csum))
			goto discard;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = ~csum_ipv6_magic(saddr, daddr, ulen, IPPROTO_UDP, 0);
#endif

	len = ulen;

	/* 
	 *	Multicast receive code 
	 */
	if (ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST) {
		udpv6_mcast_deliver(uh, saddr, daddr, skb);
		return 0;
	}

	/* Unicast */
	
	/* 
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */
	
	sk = udp_v6_lookup(saddr, uh->source, daddr, uh->dest, dev->ifindex);
	
	if (sk == NULL) {
#ifdef CONFIG_UDP_DELAY_CSUM
		if (skb->ip_summed != CHECKSUM_UNNECESSARY &&
		    (unsigned short)csum_fold(csum_partial((char*)uh, len, skb->csum)))
			goto discard;
#endif
		udp_stats_in6.UdpNoPorts++;

		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0, dev);

		kfree_skb(skb);
		return(0);
	}
	
	/* deliver */
	
	udpv6_queue_rcv_skb(sk, skb);
	
	return(0);
	
discard:
	udp_stats_in6.UdpInErrors++;
	kfree_skb(skb);
	return(0);	
}

/*
 *	Sending
 */

struct udpv6fakehdr 
{
	struct udphdr	uh;
	struct iovec	*iov;
	__u32		wcheck;
	__u32		pl_len;
	struct in6_addr *daddr;
};

/*
 *	with checksum
 */

static int udpv6_getfrag(const void *data, struct in6_addr *addr,
			 char *buff, unsigned int offset, unsigned int len)
{
	struct udpv6fakehdr *udh = (struct udpv6fakehdr *) data;
	char *dst;
	int final = 0;
	int clen = len;

	dst = buff;

	if (offset) {
		offset -= sizeof(struct udphdr);
	} else {
		dst += sizeof(struct udphdr);
		final = 1;
		clen -= sizeof(struct udphdr);
	}

	if (csum_partial_copy_fromiovecend(dst, udh->iov, offset,
					   clen, &udh->wcheck))
		return -EFAULT;

	if (final) {
		struct in6_addr *daddr;
		
		udh->wcheck = csum_partial((char *)udh, sizeof(struct udphdr),
					   udh->wcheck);

		if (udh->daddr) {
			daddr = udh->daddr;
		} else {
			/*
			 *	use packet destination address
			 *	this should improve cache locality
			 */
			daddr = addr + 1;
		}
		udh->uh.check = csum_ipv6_magic(addr, daddr,
						udh->pl_len, IPPROTO_UDP,
						udh->wcheck);
		if (udh->uh.check == 0)
			udh->uh.check = -1;

		memcpy(buff, udh, sizeof(struct udphdr));
	}
	return 0;
}

static int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, int ulen)
{
	struct ipv6_txoptions opt_space;
	struct udpv6fakehdr udh;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi fl;
	int addr_len = msg->msg_namelen;
	struct in6_addr *daddr;
	int len = ulen + sizeof(struct udphdr);
	int addr_type;
	int hlimit = -1;
	
	int err;
	
	/* Rough check on arithmetic overflow,
	   better check is made in ip6_build_xmit
	   */
	if (ulen < 0 || ulen > INT_MAX - sizeof(struct udphdr))
		return -EMSGSIZE;
	
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);

	fl.fl6_flowlabel = 0;

	if (sin6) {
		if (sin6->sin6_family == AF_INET) {
			if (__ipv6_only_sock(sk))
				return -ENETUNREACH;
			return udp_sendmsg(sk, msg, ulen);
		}

		if (addr_len < sizeof(*sin6))
			return(-EINVAL);

		if (sin6->sin6_family && sin6->sin6_family != AF_INET6)
			return(-EINVAL);

		if (sin6->sin6_port == 0)
			return(-EINVAL);

		udh.uh.dest = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->sndflow) {
			fl.fl6_flowlabel = sin6->sin6_flowinfo&IPV6_FLOWINFO_MASK;
			if (fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
				if (flowlabel == NULL)
					return -EINVAL;
				daddr = &flowlabel->dst;
			}
		}

		/* Otherwise it will be difficult to maintain sk->dst_cache. */
		if (sk->state == TCP_ESTABLISHED &&
		    !ipv6_addr_cmp(daddr, &sk->net_pinfo.af_inet6.daddr))
			daddr = &sk->net_pinfo.af_inet6.daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    sin6->sin6_scope_id &&
		    ipv6_addr_type(daddr)&IPV6_ADDR_LINKLOCAL)
			fl.oif = sin6->sin6_scope_id;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return(-ENOTCONN);

		udh.uh.dest = sk->dport;
		daddr = &sk->net_pinfo.af_inet6.daddr;
		fl.fl6_flowlabel = np->flow_label;
	}

	addr_type = ipv6_addr_type(daddr);

	if (addr_type == IPV6_ADDR_MAPPED) {
		struct sockaddr_in sin;

		if (__ipv6_only_sock(sk))
			return -ENETUNREACH;

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];
		sin.sin_port = udh.uh.dest;
		msg->msg_name = (struct sockaddr *)(&sin);
		msg->msg_namelen = sizeof(sin);
		fl6_sock_release(flowlabel);

		return udp_sendmsg(sk, msg, ulen);
	}

	udh.daddr = NULL;
	fl.oif = sk->bound_dev_if;
	fl.fl6_src = NULL;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));

		err = datagram_send_ctl(msg, &fl, opt, &hlimit);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl.fl6_flowlabel&IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl.fl6_flowlabel);
			if (flowlabel == NULL)
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
	}
	if (opt == NULL)
		opt = np->opt;
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);
	if (opt && opt->srcrt)
		udh.daddr = daddr;

	udh.uh.source = sk->sport;
	udh.uh.len = len < 0x10000 ? htons(len) : 0;
	udh.uh.check = 0;
	udh.iov = msg->msg_iov;
	udh.wcheck = 0;
	udh.pl_len = len;

	fl.proto = IPPROTO_UDP;
	fl.fl6_dst = daddr;
	fl.uli_u.ports.dport = udh.uh.dest;
	fl.uli_u.ports.sport = udh.uh.source;

	err = ip6_build_xmit(sk, udpv6_getfrag, &udh, &fl, len, opt, hlimit,
			     msg->msg_flags);

	fl6_sock_release(flowlabel);

	if (err < 0)
		return err;

	udp_stats_in6.UdpOutDatagrams++;
	return ulen;
}

static struct inet6_protocol udpv6_protocol = 
{
	udpv6_rcv,		/* UDP handler		*/
	udpv6_err,		/* UDP error control	*/
	NULL,			/* next			*/
	IPPROTO_UDP,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"UDPv6"			/* name			*/
};

#ifdef _HURD_
#define udp_ioctl 0
#endif

struct proto udpv6_prot = {
	(struct sock *)&udpv6_prot,	/* sklist_next */
	(struct sock *)&udpv6_prot,	/* sklist_prev */
	udpv6_close,			/* close */
	udpv6_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	udp_ioctl,			/* ioctl */
	NULL,				/* init */
	inet6_destroy_sock,		/* destroy */
	NULL,				/* shutdown */
	ipv6_setsockopt,		/* setsockopt */
	ipv6_getsockopt,		/* getsockopt */
	udpv6_sendmsg,			/* sendmsg */
	udpv6_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	udpv6_queue_rcv_skb,		/* backlog_rcv */
	udp_v6_hash,			/* hash */
	udp_v6_unhash,			/* unhash */
	udp_v6_get_port,		/* get_port */
	128,				/* max_header */
	0,				/* retransmits */
	"UDP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};

void __init udpv6_init(void)
{
	inet6_add_protocol(&udpv6_protocol);
}
