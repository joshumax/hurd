/*
 *	RAW sockets for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Adapted from linux/net/ipv4/raw.c
 *
 *	$Id: raw_ipv6.c,v 1.2 2007/10/13 01:43:00 stesie Exp $
 *
 *	Fixes:
 *	YOSHIFUJI,H.@USAGI	:	raw checksum (RFC2292(bis) compliance)
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
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmpv6.h>
#include <asm/uaccess.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/transp_v6.h>

#include <net/rawv6.h>

#include <asm/uaccess.h>

struct sock *raw_v6_htable[RAWV6_HTABLE_SIZE];

static void raw_v6_hash(struct sock *sk)
{
	struct sock **skp = &raw_v6_htable[sk->num & (RAWV6_HTABLE_SIZE - 1)];

	SOCKHASH_LOCK();
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	SOCKHASH_UNLOCK();
}

static void raw_v6_unhash(struct sock *sk)
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

static __inline__ int inet6_mc_check(struct sock *sk, struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc;
		
	for (mc = sk->net_pinfo.af_inet6.ipv6_mc_list; mc; mc=mc->next) {
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
			return 1;
	}

	return 0;
}

/* Grumble... icmp and ip_input want to get at this... */
struct sock *raw_v6_lookup(struct sock *sk, unsigned short num,
			   struct in6_addr *loc_addr, struct in6_addr *rmt_addr)
{
	struct sock *s = sk;
	int addr_type = ipv6_addr_type(loc_addr);

	for(s = sk; s; s = s->next) {
		if((s->num == num) 		&&
		   !(s->dead && (s->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &s->net_pinfo.af_inet6;

			if (!ipv6_addr_any(&np->daddr) &&
			    ipv6_addr_cmp(&np->daddr, rmt_addr))
				continue;

			if (!ipv6_addr_any(&np->rcv_saddr)) {
				if (ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
					return(s);
				if ((addr_type & IPV6_ADDR_MULTICAST) &&
				    inet6_mc_check(s, loc_addr))
					return (s);
				continue;
			}
			return(s);
		}
	}
	return NULL;
}

/* This cleans up af_inet6 a bit. -DaveM */
static int rawv6_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *) uaddr;
	__u32 v4addr = 0;
	int addr_type;

	/* Check these errors. */
	if (sk->state != TCP_CLOSE || (addr_len < sizeof(struct sockaddr_in6)))
		return -EINVAL;

	addr_type = ipv6_addr_type(&addr->sin6_addr);

	/* Check if the address belongs to the host. */
	if (addr_type == IPV6_ADDR_MAPPED) {
		/* Raw sockets are IPv6 only */
		return(-EADDRNOTAVAIL);
	} else {
		if (addr_type != IPV6_ADDR_ANY) {
			if (addr_type & IPV6_ADDR_LINKLOCAL) {
				if (addr_len >= sizeof(struct sockaddr_in6) &&
				    addr->sin6_scope_id) {
					/* Override any existing binding,
					 * if another one is supplied by user.
					 */
					sk->bound_dev_if =
						addr->sin6_scope_id;
				}

				/* Binding to link-local address requires
				   an interface */
				if (!sk->bound_dev_if)
					return(-EINVAL);
				
				if (!dev_get_by_index(sk->bound_dev_if))
					return(-ENODEV);
			}

			/* ipv4 addr of the socket is invalid.  Only the
			 * unpecified and mapped address have a v4 equivalent.
			 */
			v4addr = LOOPBACK4_IPV6;
			if (!(addr_type & IPV6_ADDR_MULTICAST))	{
				if (ipv6_chk_addr(&addr->sin6_addr, NULL, 0) == NULL)
					return(-EADDRNOTAVAIL);
			}
		}
	}

	sk->rcv_saddr = v4addr;
	sk->saddr = v4addr;
	memcpy(&sk->net_pinfo.af_inet6.rcv_saddr, &addr->sin6_addr, 
	       sizeof(struct in6_addr));
	if (!(addr_type & IPV6_ADDR_MULTICAST))
		memcpy(&sk->net_pinfo.af_inet6.saddr, &addr->sin6_addr, 
		       sizeof(struct in6_addr));
	return 0;
}

void rawv6_err(struct sock *sk, struct sk_buff *skb, struct ipv6hdr *hdr,
	       struct inet6_skb_parm *opt,
	       int type, int code, unsigned char *buff, u32 info)
{
	int err;
	int harderr;

	if (buff > skb->tail)
		return;

	/* Report error on raw socket, if:
	   1. User requested recverr.
	   2. Socket is connected (otherwise the error indication
	      is useless without recverr and error is hard.
	 */
	if (!sk->net_pinfo.af_inet6.recverr && sk->state != TCP_ESTABLISHED)
		return;

	harderr = icmpv6_err_convert(type, code, &err);
	if (type == ICMPV6_PKT_TOOBIG)
		harderr = (sk->net_pinfo.af_inet6.pmtudisc == IPV6_PMTUDISC_DO);

	if (sk->net_pinfo.af_inet6.recverr)
		ipv6_icmp_error(sk, skb, err, 0, ntohl(info), buff);

	if (sk->net_pinfo.af_inet6.recverr || harderr) {
		sk->err = err;
		sk->error_report(sk);
	}
}

static inline int rawv6_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	if (sock_queue_rcv_skb(sk,skb)<0) {
		ipv6_statistics.Ip6InDiscards++;
		kfree_skb(skb);
		return 0;
	}

	ipv6_statistics.Ip6InDelivers++;
	return 0;
}

/*
 *	This is next to useless... 
 *	if we demultiplex in network layer we don't need the extra call
 *	just to queue the skb... 
 *	maybe we could have the network decide uppon a hint if it 
 *	should call raw_rcv for demultiplexing
 */
int rawv6_rcv(struct sock *sk, struct sk_buff *skb, unsigned long len)
{
	if (sk->ip_hdrincl)
		skb->h.raw = skb->nh.raw;

	rawv6_rcv_skb(sk, skb);
	return 0;
}


/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int rawv6_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags, int *addr_len)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)msg->msg_name;
	struct sk_buff *skb;
	int copied, err;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;
		
	if (addr_len) 
		*addr_len=sizeof(*sin6);

	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	copied = skb->tail - skb->h.raw;
  	if (copied > len) {
  		copied = len;
  		msg->msg_flags |= MSG_TRUNC;
  	}

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	sk->stamp=skb->stamp;
	if (err)
		goto out_free;

	/* Copy the address. */
	if (sin6) {
		sin6->sin6_family = AF_INET6;
		memcpy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr, 
		       sizeof(struct in6_addr));
		sin6->sin6_flowinfo = 0;
		sin6->sin6_scope_id = 0;
		if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
			sin6->sin6_scope_id = 
				((struct inet6_skb_parm *) skb->cb)->iif;
	}

	if (sk->net_pinfo.af_inet6.rxopt.all)
		datagram_recv_ctl(sk, msg, skb);
	err = copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;
}

/*
 *	Sending...
 */

struct rawv6_fakehdr {
	struct iovec	*iov;
	struct sock	*sk;
	__u32		len;
	__u32		cksum;
	__u32		proto;
	struct in6_addr *daddr;
};

static int rawv6_getfrag(const void *data, struct in6_addr *saddr, 
			  char *buff, unsigned int offset, unsigned int len)
{
	struct iovec *iov = (struct iovec *) data;

	return memcpy_fromiovecend(buff, iov, offset, len);
}

static int rawv6_frag_cksum(const void *data, struct in6_addr *addr,
			     char *buff, unsigned int offset, 
			     unsigned int len)
{
	struct rawv6_fakehdr *hdr = (struct rawv6_fakehdr *) data;
	
	if (csum_partial_copy_fromiovecend(buff, hdr->iov, offset, 
						    len, &hdr->cksum))
		return -EFAULT;
	
	if (offset == 0) {
		struct sock *sk;
		struct raw6_opt *opt;
		struct in6_addr *daddr;
		
		sk = hdr->sk;
		opt = &sk->tp_pinfo.tp_raw;

		if (hdr->daddr)
			daddr = hdr->daddr;
		else
			daddr = addr + 1;
		
		hdr->cksum = csum_ipv6_magic(addr, daddr, hdr->len,
					     hdr->proto, hdr->cksum);
		
		if (opt->offset + 1 < len) {
			__u16 *csum;

			csum = (__u16 *) (buff + opt->offset);
			if (*csum) {
				/* in case cksum was not initialized */
				__u32 sum = hdr->cksum;
				sum += *csum;
				*csum = hdr->cksum = (sum + (sum>>16));
			} else {
				*csum = hdr->cksum;
			}
		} else {
			if (net_ratelimit())
				printk(KERN_DEBUG "icmp: cksum offset too big\n");
			return -EINVAL;
		}
	}	
	return 0; 
}


static int rawv6_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	struct ipv6_txoptions opt_space;
	struct sockaddr_in6 * sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_txoptions *opt = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi fl;
	int addr_len = msg->msg_namelen;
	struct in6_addr *daddr;
	struct raw6_opt *raw_opt;
	int hlimit = -1;
	u16 proto;
	int err;

	/* Rough check on arithmetic overflow,
	   better check is made in ip6_build_xmit
	 */
	if (len < 0)
		return -EMSGSIZE;

	/* Mirror BSD error message compatibility */
	if (msg->msg_flags & MSG_OOB)		
		return -EOPNOTSUPP;
			 
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);
	/*
	 *	Get and verify the address. 
	 */

	fl.fl6_flowlabel = 0;

	if (sin6) {
		if (addr_len < sizeof(struct sockaddr_in6)) 
			return(-EINVAL);

		if (sin6->sin6_family && sin6->sin6_family != AF_INET6) 
			return(-EINVAL);
		
		/* port is the proto value [0..255] carried in nexthdr */
		proto = ntohs(sin6->sin6_port);

		if (!proto)
			proto = sk->num;

		if (proto > 255)
			return(-EINVAL);

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
			return(-EINVAL);
		
		proto = sk->num;
		daddr = &(sk->net_pinfo.af_inet6.daddr);
		fl.fl6_flowlabel = np->flow_label;
	}

	if (ipv6_addr_any(daddr)) {
		/* 
		 * unspecfied destination address 
		 * treated as error... is this correct ?
		 */
		return(-EINVAL);
	}

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

	raw_opt = &sk->tp_pinfo.tp_raw;

	fl.proto = proto;
	fl.fl6_dst = daddr;
	fl.uli_u.icmpt.type = 0;
	fl.uli_u.icmpt.code = 0;
	
	if (raw_opt->checksum) {
		struct rawv6_fakehdr hdr;
		
		hdr.iov = msg->msg_iov;
		hdr.sk  = sk;
		hdr.len = len;
		hdr.cksum = 0;
		hdr.proto = proto;

		if (opt && opt->srcrt)
			hdr.daddr = daddr;
		else
			hdr.daddr = NULL;

		err = ip6_build_xmit(sk, rawv6_frag_cksum, &hdr, &fl, len,
				     opt, hlimit, msg->msg_flags);
	} else {
		err = ip6_build_xmit(sk, rawv6_getfrag, msg->msg_iov, &fl, len,
				     opt, hlimit, msg->msg_flags);
	}

	fl6_sock_release(flowlabel);

	return err<0?err:len;
}

static int rawv6_seticmpfilter(struct sock *sk, int level, int optname, 
			       char *optval, int optlen)
{
	switch (optname) {
	case ICMPV6_FILTER:
		if (optlen > sizeof(struct icmp6_filter))
			optlen = sizeof(struct icmp6_filter);
		if (copy_from_user(&sk->tp_pinfo.tp_raw.filter, optval, optlen))
			return -EFAULT;
		return 0;
	default:
		return -ENOPROTOOPT;
	};

	return 0;
}

static int rawv6_geticmpfilter(struct sock *sk, int level, int optname, 
			       char *optval, int *optlen)
{
	int len;

	switch (optname) {
	case ICMPV6_FILTER:
		if (get_user(len, optlen))
			return -EFAULT;
		if (len > sizeof(struct icmp6_filter))
			len = sizeof(struct icmp6_filter);
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &sk->tp_pinfo.tp_raw.filter, len))
			return -EFAULT;
		return 0;
	default:
		return -ENOPROTOOPT;
	};

	return 0;
}


static int rawv6_setsockopt(struct sock *sk, int level, int optname, 
			    char *optval, int optlen)
{
	struct raw6_opt *opt = &sk->tp_pinfo.tp_raw;
	int val;

	switch(level) {
		case SOL_RAW:
			break;

		case SOL_ICMPV6:
			if (sk->num != IPPROTO_ICMPV6)
				return -EOPNOTSUPP;
			return rawv6_seticmpfilter(sk, level, optname, optval,
						   optlen);
		case SOL_IPV6:
			if (optname == IPV6_CHECKSUM)
				break;
		default:
			return ipv6_setsockopt(sk, level, optname, optval,
					       optlen);
	};

  	if (get_user(val, (int *)optval))
		return -EFAULT;

	switch (optname) {
		case IPV6_CHECKSUM:
			/* You may get strange result with a positive odd offset;
			   RFC2292bis agrees with me. */
			if (val > 0 && (val&1))
				return(-EINVAL);
			if (val < 0) {
				opt->checksum = 0;
			} else {
				opt->checksum = 1;
				opt->offset = val;
			}

			return 0;
			break;

		default:
			return(-ENOPROTOOPT);
	}
}

static int rawv6_getsockopt(struct sock *sk, int level, int optname, 
			    char *optval, int *optlen)
{
	struct raw6_opt *opt = &sk->tp_pinfo.tp_raw;
	int val, len;

	switch(level) {
		case SOL_RAW:
			break;

		case SOL_ICMPV6:
			if (sk->num != IPPROTO_ICMPV6)
				return -EOPNOTSUPP;
			return rawv6_geticmpfilter(sk, level, optname, optval,
						   optlen);
		case SOL_IPV6:
			if (optname == IPV6_CHECKSUM)
				break;
		default:
			return ipv6_getsockopt(sk, level, optname, optval,
					       optlen);
	};

	if (get_user(len,optlen))
		return -EFAULT;

	switch (optname) {
	case IPV6_CHECKSUM:
		if (opt->checksum == 0)
			val = -1;
		else
			val = opt->offset;

	default:
		return -ENOPROTOOPT;
	}

	len=min(sizeof(int),len);

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval,&val,len))
		return -EFAULT;
	return 0;
}


static void rawv6_close(struct sock *sk, long timeout)
{
	/* See for explanation: raw_close in ipv4/raw.c */
	sk->state = TCP_CLOSE;
	raw_v6_unhash(sk);
	if (sk->num == IPPROTO_RAW)
		ip6_ra_control(sk, -1, NULL);
	sk->dead = 1;
	destroy_sock(sk);
}

static int rawv6_init_sk(struct sock *sk)
{
	if (sk->num == IPPROTO_ICMPV6){
		struct raw6_opt *opt = &sk->tp_pinfo.tp_raw;
		opt->checksum = 1;
		opt->offset = 2;
	}
	return(0);
}

struct proto rawv6_prot = {
	(struct sock *)&rawv6_prot,	/* sklist_next */
	(struct sock *)&rawv6_prot,	/* sklist_prev */
	rawv6_close,			/* close */
	udpv6_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	NULL,				/* ioctl */
	rawv6_init_sk,			/* init */
	inet6_destroy_sock,		/* destroy */
	NULL,				/* shutdown */
	rawv6_setsockopt,		/* setsockopt */
	rawv6_getsockopt,		/* getsockopt */
	rawv6_sendmsg,			/* sendmsg */
	rawv6_recvmsg,			/* recvmsg */
	rawv6_bind,			/* bind */
	rawv6_rcv_skb,			/* backlog_rcv */
	raw_v6_hash,			/* hash */
	raw_v6_unhash,			/* unhash */
	NULL,				/* get_port */
	128,				/* max_header */
	0,				/* retransmits */
	"RAW",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};
