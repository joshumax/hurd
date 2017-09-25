/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The User Datagram Protocol (UDP).
 *
 * Version:	$Id: udp.c,v 1.66.2.3 1999/08/07 10:56:36 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Alan Cox, <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() calls
 *		Alan Cox	: 	stopped close while in use off icmp
 *					messages. Not a fix but a botch that
 *					for udp at least is 'valid'.
 *		Alan Cox	:	Fixed icmp handling properly
 *		Alan Cox	: 	Correct error for oversized datagrams
 *		Alan Cox	:	Tidied select() semantics.
 *		Alan Cox	:	udp_err() fixed properly, also now
 *					select and read wake correctly on errors
 *		Alan Cox	:	udp_send verify_area moved to avoid mem leak
 *		Alan Cox	:	UDP can count its memory
 *		Alan Cox	:	send to an unknown connection causes
 *					an ECONNREFUSED off the icmp, but
 *					does NOT close.
 *		Alan Cox	:	Switched to new sk_buff handlers. No more backlog!
 *		Alan Cox	:	Using generic datagram code. Even smaller and the PEEK
 *					bug no longer crashes it.
 *		Fred Van Kempen	: 	Net2e support for sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram
 *		Alan Cox	:	Added get/set sockopt support.
 *		Alan Cox	:	Broadcasting without option set returns EACCES.
 *		Alan Cox	:	No wakeup calls. Instead we now use the callbacks.
 *		Alan Cox	:	Use ip_tos and ip_ttl
 *		Alan Cox	:	SNMP Mibs
 *		Alan Cox	:	MSG_DONTROUTE, and 0.0.0.0 support.
 *		Matt Dillon	:	UDP length checks.
 *		Alan Cox	:	Smarter af_inet used properly.
 *		Alan Cox	:	Use new kernel side addressing.
 *		Alan Cox	:	Incorrect return on truncated datagram receive.
 *	Arnt Gulbrandsen 	:	New udp_send and stuff
 *		Alan Cox	:	Cache last socket
 *		Alan Cox	:	Route cache
 *		Jon Peatfield	:	Minor efficiency fix to sendto().
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Nonblocking error fix.
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Mike McLagan	:	Routing by source
 *		David S. Miller	:	New socket lookup architecture.
 *					Last socket cache retained as it
 *					does have a high hit rate.
 *		Olaf Kirch	:	Don't linearise iovec on sendmsg.
 *		Andi Kleen	:	Some cleanups, cache destination entry
 *					for connect.
 *	Vitaly E. Lavrov	:	Transparent proxy revived after year coma.
 *		Melvin Smith	:	Check msg_name not msg_namelen in sendto(),
 *					return ENOTCONN for unconnected sockets (POSIX)
 *		Janos Farkas	:	don't deliver multi/broadcasts to a different
 *					bound-to-device socket
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov:		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/* RFC1122 Status:
   4.1.3.1 (Ports):
     SHOULD send ICMP_PORT_UNREACHABLE in response to datagrams to
       an un-listened port. (OK)
   4.1.3.2 (IP Options)
     MUST pass IP options from IP -> application (OK)
     MUST allow application to specify IP options (OK)
   4.1.3.3 (ICMP Messages)
     MUST pass ICMP error messages to application (OK -- except when SO_BSDCOMPAT is set)
   4.1.3.4 (UDP Checksums)
     MUST provide facility for checksumming (OK)
     MAY allow application to control checksumming (OK)
     MUST default to checksumming on (OK)
     MUST discard silently datagrams with bad csums (OK, except during debugging)
   4.1.3.5 (UDP Multihoming)
     MUST allow application to specify source address (OK)
     SHOULD be able to communicate the chosen src addr up to application
       when application doesn't choose (DOES - use recvmsg cmsgs)
   4.1.3.6 (Invalid Addresses)
     MUST discard invalid source addresses (OK -- done in the new routing code)
     MUST only send datagrams with one of our addresses (OK)
*/

#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/inet.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/checksum.h>

/*
 *	Snmp MIB for the UDP layer
 */

struct udp_mib		udp_statistics;

struct sock *udp_hash[UDP_HTABLE_SIZE];

static int udp_v4_get_port(struct sock *sk, unsigned short snum)
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
			; /* Do nothing.  */
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

		for (sk2 = udp_hash[snum & (UDP_HTABLE_SIZE - 1)];
		     sk2 != NULL;
		     sk2 = sk2->next) {
			if (sk2->num == snum &&
			    sk2 != sk &&
			    !ipv6_only_sock(sk2) &&
			    sk2->bound_dev_if == sk->bound_dev_if &&
			    (!sk2->rcv_saddr ||
			     !sk->rcv_saddr ||
			     sk2->rcv_saddr == sk->rcv_saddr) &&
			    (!sk2->reuse || !sk->reuse))
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

/* Last hit UDP socket cache, this is ipv4 specific so make it static. */
static u32 uh_cache_saddr, uh_cache_daddr;
static u16 uh_cache_dport, uh_cache_sport;
static struct sock *uh_cache_sk = NULL;

static void udp_v4_hash(struct sock *sk)
{
	struct sock **skp = &udp_hash[sk->num & (UDP_HTABLE_SIZE - 1)];

	SOCKHASH_LOCK();
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	SOCKHASH_UNLOCK();
}

static void udp_v4_unhash(struct sock *sk)
{
	SOCKHASH_LOCK();
	if (sk->pprev) {
		if (sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		if(uh_cache_sk == sk)
			uh_cache_sk = NULL;
	}
	SOCKHASH_UNLOCK();
}

/* UDP is nearly always wildcards out the wazoo, it makes no sense to try
 * harder than this here plus the last hit cache. -DaveM
 */
struct sock *udp_v4_lookup_longway(u32 saddr, u16 sport, u32 daddr, u16 dport, int dif)
{
	struct sock *sk, *result = NULL;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

	for(sk = udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]; sk != NULL; sk = sk->next) {
	  if((sk->num == hnum) && !ipv6_only_sock(sk)
	      && !(sk->dead && (sk->state == TCP_CLOSE))) {
			int score = (sk->family == PF_INET ? 1 : 0);
			if(sk->rcv_saddr) {
				if(sk->rcv_saddr != daddr)
					continue;
				score+=2;
			}
			if(sk->daddr) {
				if(sk->daddr != saddr)
					continue;
				score+=2;
			}
			if(sk->dport) {
				if(sk->dport != sport)
					continue;
				score+=2;
			}
			if(sk->bound_dev_if) {
				if(sk->bound_dev_if != dif)
					continue;
				score+=2;
			}
			if(score == 9) {
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

__inline__ struct sock *udp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport, int dif)
{
	struct sock *sk;

	if(!dif && uh_cache_sk		&&
	   uh_cache_saddr == saddr	&&
	   uh_cache_sport == sport	&&
	   uh_cache_dport == dport	&&
	   uh_cache_daddr == daddr)
		return uh_cache_sk;

	sk = udp_v4_lookup_longway(saddr, sport, daddr, dport, dif);
	if(!dif) {
		uh_cache_sk	= sk;
		uh_cache_saddr	= saddr;
		uh_cache_daddr	= daddr;
		uh_cache_sport	= sport;
		uh_cache_dport	= dport;
	}
	return sk;
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
#define secondlist(hpnum, sk, fpass) \
({ struct sock *s1; if(!(sk) && (fpass)--) \
	s1 = udp_hash[(hpnum) & (UDP_HTABLE_SIZE - 1)]; \
   else \
	s1 = (sk); \
   s1; \
})

#define udp_v4_proxy_loop_init(hnum, hpnum, sk, fpass) \
	secondlist((hpnum), udp_hash[(hnum)&(UDP_HTABLE_SIZE-1)],(fpass))

#define udp_v4_proxy_loop_next(hnum, hpnum, sk, fpass) \
	secondlist((hpnum),(sk)->next,(fpass))

static struct sock *udp_v4_proxy_lookup(unsigned short num, unsigned long raddr,
					unsigned short rnum, unsigned long laddr,
					struct device *dev, unsigned short pnum,
					int dif)
{
	struct sock *s, *result = NULL;
	int badness = -1;
	u32 paddr = 0;
	unsigned short hnum = ntohs(num);
	unsigned short hpnum = ntohs(pnum);
	int firstpass = 1;

	if(dev && dev->ip_ptr) {
		struct in_device *idev = dev->ip_ptr;

		if(idev->ifa_list)
			paddr = idev->ifa_list->ifa_local;
	}

	SOCKHASH_LOCK();
	for(s = udp_v4_proxy_loop_init(hnum, hpnum, s, firstpass);
	    s != NULL;
	    s = udp_v4_proxy_loop_next(hnum, hpnum, s, firstpass)) {
		if(s->num == hnum || s->num == hpnum) {
			int score = 0;
			if(s->dead && (s->state == TCP_CLOSE))
				continue;
			if(s->rcv_saddr) {
				if((s->num != hpnum || s->rcv_saddr != paddr) &&
				   (s->num != hnum || s->rcv_saddr != laddr))
					continue;
				score++;
			}
			if(s->daddr) {
				if(s->daddr != raddr)
					continue;
				score++;
			}
			if(s->dport) {
				if(s->dport != rnum)
					continue;
				score++;
			}
			if(s->bound_dev_if) {
				if(s->bound_dev_if != dif)
					continue;
				score++;
			}
			if(score == 4 && s->num == hnum) {
				result = s;
				break;
			} else if(score > badness && (s->num == hpnum || s->rcv_saddr)) {
					result = s;
					badness = score;
			}
		}
	}
	SOCKHASH_UNLOCK();
	return result;
}

#undef secondlist
#undef udp_v4_proxy_loop_init
#undef udp_v4_proxy_loop_next

#endif

static inline struct sock *udp_v4_mcast_next(struct sock *sk,
					     unsigned short num,
					     unsigned long raddr,
					     unsigned short rnum,
					     unsigned long laddr,
					     int dif)
{
	struct sock *s = sk;
	unsigned short hnum = ntohs(num);
	for(; s; s = s->next) {
		if ((s->num != hnum)					||
		    (s->dead && (s->state == TCP_CLOSE))		||
		    (s->daddr && s->daddr!=raddr)			||
		    (s->dport != rnum && s->dport != 0)			||
		    (s->rcv_saddr  && s->rcv_saddr != laddr)		||
		    ipv6_only_sock(s)					||
		    (s->bound_dev_if && s->bound_dev_if != dif))
			continue;
		break;
  	}
  	return s;
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.
 * Header points to the ip header of the error packet. We move
 * on past this. Then (as it used to claim before adjustment)
 * header points to the first 8 bytes of the udp header.  We need
 * to find the appropriate port.
 */

void udp_err(struct sk_buff *skb, unsigned char *dp, int len)
{
	struct iphdr *iph = (struct iphdr*)dp;
	struct udphdr *uh = (struct udphdr*)(dp+(iph->ihl<<2));
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	struct sock *sk;
	int harderr;
	u32 info;
	int err;

	if (len < (iph->ihl<<2)+sizeof(struct udphdr)) {
		icmp_statistics.IcmpInErrors++;
		return;
	}

	sk = udp_v4_lookup(iph->daddr, uh->dest, iph->saddr, uh->source, skb->dev->ifindex);
	if (sk == NULL) {
		icmp_statistics.IcmpInErrors++;
    	  	return;	/* No socket for error */
	}

	err = 0;
	info = 0;
	harderr = 0;

	switch (type) {
	default:
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	case ICMP_SOURCE_QUENCH:
		return;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		info = ntohl(skb->h.icmph->un.gateway)>>24;
		harderr = 1;
		break;
	case ICMP_DEST_UNREACH:
		if (code == ICMP_FRAG_NEEDED) { /* Path MTU discovery */
			if (sk->ip_pmtudisc != IP_PMTUDISC_DONT) {
				err = EMSGSIZE;
				info = ntohs(skb->h.icmph->un.frag.mtu);
				harderr = 1;
				break;
			}
			return;
		}
		err = EHOSTUNREACH;
		if (code <= NR_ICMP_UNREACH) {
			harderr = icmp_err_convert[code].fatal;
			err = icmp_err_convert[code].errno;
		}
		break;
	}

	/*
	 *      RFC1122: OK.  Passes ICMP errors back to application, as per
	 *	4.1.3.3.
	 */
	if (!sk->ip_recverr) {
		if (!harderr || sk->state != TCP_ESTABLISHED)
			return;
	} else {
		ip_icmp_error(sk, skb, err, uh->dest, info, (u8*)(uh+1));
	}
	sk->err = err;
	sk->error_report(sk);
}


static unsigned short udp_check(struct udphdr *uh, int len, unsigned long saddr, unsigned long daddr, unsigned long base)
{
	return(csum_tcpudp_magic(saddr, daddr, len, IPPROTO_UDP, base));
}

struct udpfakehdr
{
	struct udphdr uh;
	u32 saddr;
	u32 daddr;
	struct iovec *iov;
	u32 wcheck;
};

/*
 *	Copy and checksum a UDP packet from user space into a buffer. We still have
 *	to do the planning to get ip_build_xmit to spot direct transfer to network
 *	card and provide an additional callback mode for direct user->board I/O
 *	transfers. That one will be fun.
 */

static int udp_getfrag(const void *p, char * to, unsigned int offset, unsigned int fraglen)
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;
	if (offset==0) {
		if (csum_partial_copy_fromiovecend(to+sizeof(struct udphdr), ufh->iov, offset,
						   fraglen-sizeof(struct udphdr), &ufh->wcheck))
			return -EFAULT;
 		ufh->wcheck = csum_partial((char *)ufh, sizeof(struct udphdr),
					   ufh->wcheck);
		ufh->uh.check = csum_tcpudp_magic(ufh->saddr, ufh->daddr,
					  ntohs(ufh->uh.len),
					  IPPROTO_UDP, ufh->wcheck);
		if (ufh->uh.check == 0)
			ufh->uh.check = -1;
		memcpy(to, ufh, sizeof(struct udphdr));
		return 0;
	}
	if (csum_partial_copy_fromiovecend(to, ufh->iov, offset-sizeof(struct udphdr),
					   fraglen, &ufh->wcheck))
		return -EFAULT;
	return 0;
}

/*
 *	Unchecksummed UDP is sufficiently critical to stuff like ATM video conferencing
 *	that we use two routines for this for speed. Probably we ought to have a
 *	CONFIG_FAST_NET set for >10Mb/second boards to activate this sort of coding.
 *	Timing needed to verify if this is a valid decision.
 */

static int udp_getfrag_nosum(const void *p, char * to, unsigned int offset, unsigned int fraglen)
{
	struct udpfakehdr *ufh = (struct udpfakehdr *)p;

	if (offset==0) {
		memcpy(to, ufh, sizeof(struct udphdr));
		return memcpy_fromiovecend(to+sizeof(struct udphdr), ufh->iov, offset,
					   fraglen-sizeof(struct udphdr));
	}
	return memcpy_fromiovecend(to, ufh->iov, offset-sizeof(struct udphdr),
				   fraglen);
}

int udp_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	int ulen = len + sizeof(struct udphdr);
	struct ipcm_cookie ipc;
	struct udpfakehdr ufh;
	struct rtable *rt = NULL;
	int free = 0;
	int connected = 0;
	u32 daddr;
	u8  tos;
	int err;

	/* This check is ONLY to check for arithmetic overflow
	   on integer(!) len. Not more! Real check will be made
	   in ip_build_xmit --ANK

	   BTW socket.c -> af_*.c -> ... make multiple
	   invalid conversions size_t -> int. We MUST repair it f.e.
	   by replacing all of them with size_t and revise all
	   the places sort of len += sizeof(struct iphdr)
	   If len was ULONG_MAX-10 it would be cathastrophe  --ANK
	 */

	if (len < 0 || len > 0xFFFF)
		return -EMSGSIZE;

	/*
	 *	Check the flags.
	 */

	if (msg->msg_flags&MSG_OOB)	/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (msg->msg_flags&~(MSG_DONTROUTE|MSG_DONTWAIT|MSG_PROXY|MSG_NOSIGNAL))
	  	return -EINVAL;
	if ((msg->msg_flags&MSG_PROXY) && !capable(CAP_NET_ADMIN))
	  	return -EPERM;
#else
	if (msg->msg_flags&~(MSG_DONTROUTE|MSG_DONTWAIT|MSG_NOSIGNAL))
	  	return -EINVAL;
#endif

	/*
	 *	Get and verify the address.
	 */

	if (msg->msg_name) {
		struct sockaddr_in * usin = (struct sockaddr_in*)msg->msg_name;
		if (msg->msg_namelen < sizeof(*usin))
			return(-EINVAL);
		if (usin->sin_family != AF_INET) {
			static int complained;
			if (!complained++)
				printk(KERN_WARNING "%s forgot to set AF_INET in udp sendmsg. Fix it!\n", current->comm);
			if (usin->sin_family)
				return -EINVAL;
		}
		ufh.daddr = usin->sin_addr.s_addr;
		ufh.uh.dest = usin->sin_port;
		if (ufh.uh.dest == 0)
			return -EINVAL;
	} else {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		ufh.daddr = sk->daddr;
		ufh.uh.dest = sk->dport;
		/* Open fast path for connected socket.
		   Route will not be used, if at least one option is set.
		 */
		connected = 1;
  	}
#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (msg->msg_flags&MSG_PROXY) {
		/*
		 * We map the first 8 bytes of a second sockaddr_in
		 * into the last 8 (unused) bytes of a sockaddr_in.
		 */
		struct sockaddr_in *from = (struct sockaddr_in *)msg->msg_name;
		from = (struct sockaddr_in *)&from->sin_zero;
		if (from->sin_family != AF_INET)
			return -EINVAL;
		ipc.addr = from->sin_addr.s_addr;
		ufh.uh.source = from->sin_port;
		if (ipc.addr == 0)
			ipc.addr = sk->saddr;
		connected = 0;
	} else
#endif
	{
		ipc.addr = sk->saddr;
		ufh.uh.source = sk->sport;
	}

	ipc.opt = NULL;
	ipc.oif = sk->bound_dev_if;
	if (msg->msg_controllen) {
		err = ip_cmsg_send(msg, &ipc);
		if (err)
			return err;
		if (ipc.opt)
			free = 1;
		connected = 0;
	}
	if (!ipc.opt)
		ipc.opt = sk->opt;

	ufh.saddr = ipc.addr;
	ipc.addr = daddr = ufh.daddr;

	if (ipc.opt && ipc.opt->srr) {
		if (!daddr)
			return -EINVAL;
		daddr = ipc.opt->faddr;
		connected = 0;
	}
	tos = RT_TOS(sk->ip_tos);
	if (sk->localroute || (msg->msg_flags&MSG_DONTROUTE) ||
	    (ipc.opt && ipc.opt->is_strictroute)) {
		tos |= RTO_ONLINK;
		connected = 0;
	}

	if (MULTICAST(daddr)) {
		if (!ipc.oif)
			ipc.oif = sk->ip_mc_index;
		if (!ufh.saddr)
			ufh.saddr = sk->ip_mc_addr;
		connected = 0;
	}

	if (connected && sk->dst_cache) {
		rt = (struct rtable*)sk->dst_cache;
		if (rt->u.dst.obsolete) {
			sk->dst_cache = NULL;
			dst_release(&rt->u.dst);
			rt = NULL;
		} else
			dst_clone(&rt->u.dst);
	}

	if (rt == NULL) {
		err = ip_route_output(&rt, daddr, ufh.saddr,
#ifdef CONFIG_IP_TRANSPARENT_PROXY
			(msg->msg_flags&MSG_PROXY ? RTO_TPROXY : 0) |
#endif
			 tos, ipc.oif);
		if (err)
			goto out;

		err = -EACCES;
		if (rt->rt_flags&RTCF_BROADCAST && !sk->broadcast)
			goto out;
		if (connected && sk->dst_cache == NULL)
			sk->dst_cache = dst_clone(&rt->u.dst);
	}

	ufh.saddr = rt->rt_src;
	if (!ipc.addr)
		ufh.daddr = ipc.addr = rt->rt_dst;
	ufh.uh.len = htons(ulen);
	ufh.uh.check = 0;
	ufh.iov = msg->msg_iov;
	ufh.wcheck = 0;

	/* RFC1122: OK.  Provides the checksumming facility (MUST) as per */
	/* 4.1.3.4. It's configurable by the application via setsockopt() */
	/* (MAY) and it defaults to on (MUST). */

	err = ip_build_xmit(sk,sk->no_check ? udp_getfrag_nosum : udp_getfrag,
			    &ufh, ulen, &ipc, rt, msg->msg_flags);

out:
	ip_rt_put(rt);
	if (free)
		kfree(ipc.opt);
	if (!err) {
		udp_statistics.UdpOutDatagrams++;
		return len;
	}
	return err;
}

#ifdef _HURD_

#define udp_ioctl 0

#else

/*
 *	IOCTL requests applicable to the UDP protocol
 */

int udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case TIOCOUTQ:
		{
			unsigned long amount;

			amount = sock_wspace(sk);
			return put_user(amount, (int *)arg);
		}

		case TIOCINQ:
		{
			struct sk_buff *skb;
			unsigned long amount;

			amount = 0;
			/* N.B. Is this interrupt safe??
			   -> Yes. Interrupts do not remove skbs. --ANK (980725)
			 */
			skb = skb_peek(&sk->receive_queue);
			if (skb != NULL) {
				/*
				 * We will only return the amount
				 * of this packet since that is all
				 * that will be read.
				 */
				amount = skb->len - sizeof(struct udphdr);
			}
			return put_user(amount, (int *)arg);
		}

		default:
			return(-ENOIOCTLCMD);
	}
	return(0);
}

#endif

#ifndef HAVE_CSUM_COPY_USER
#undef CONFIG_UDP_DELAY_CSUM
#endif

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

int udp_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		int noblock, int flags, int *addr_len)
{
  	struct sockaddr_in *sin = (struct sockaddr_in *)msg->msg_name;
  	struct sk_buff *skb;
  	int copied, err;

	if (flags & MSG_ERRQUEUE)
		return ip_recv_error(sk, msg, len);

	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

  	copied = skb->len - sizeof(struct udphdr);
	if (copied > len) {
		copied = len;
		msg->msg_flags |= MSG_TRUNC;
	}

#ifndef CONFIG_UDP_DELAY_CSUM
	err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					copied);
#else
	if (skb->ip_summed==CHECKSUM_UNNECESSARY) {
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else if (copied > msg->msg_iov[0].iov_len || (msg->msg_flags&MSG_TRUNC)) {
		if ((unsigned short)csum_fold(csum_partial(skb->h.raw, skb->len, skb->csum)))
			goto csum_copy_err;
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else {
		unsigned int csum;

		err = 0;
		csum = csum_partial(skb->h.raw, sizeof(struct udphdr), skb->csum);
		csum = csum_and_copy_to_user((char*)&skb->h.uh[1], msg->msg_iov[0].iov_base,
					     copied, csum, &err);
		if (err)
			goto out_free;
		if ((unsigned short)csum_fold(csum))
			goto csum_copy_err;
	}
#endif
	if (err)
		goto out_free;
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin)
	{
		/*
		 *	Check any passed addresses
		 */
		if (addr_len)
			*addr_len=sizeof(*sin);

		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
#ifdef CONFIG_IP_TRANSPARENT_PROXY
		if (flags&MSG_PROXY)
		{
			/*
			 * We map the first 8 bytes of a second sockaddr_in
			 * into the last 8 (unused) bytes of a sockaddr_in.
			 * This _is_ ugly, but it's the only way to do it
			 * easily,  without adding system calls.
			 */
			struct sockaddr_in *sinto =
				(struct sockaddr_in *) sin->sin_zero;

			sinto->sin_family = AF_INET;
			sinto->sin_port = skb->h.uh->dest;
			sinto->sin_addr.s_addr = skb->nh.iph->daddr;
		}
#endif
  	}
	if (sk->ip_cmsg_flags)
		ip_cmsg_recv(msg, skb);
	err = copied;

out_free:
  	skb_free_datagram(sk, skb);
out:
  	return err;

#ifdef CONFIG_UDP_DELAY_CSUM
csum_copy_err:
	udp_statistics.UdpInErrors++;
	skb_free_datagram(sk, skb);

	/*
	 * Error for blocking case is chosen to masquerade
   	 * as some normal condition.
	 */
	return (flags&MSG_DONTWAIT) ? -EAGAIN : -EHOSTUNREACH;
#endif
}

int udp_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;
	struct rtable *rt;
	int err;


	if (addr_len < sizeof(*usin))
	  	return(-EINVAL);

	/*
	 *	1003.1g - break association.
	 */

	if (usin->sin_family==AF_UNSPEC)
	{
		sk->saddr=INADDR_ANY;
		sk->rcv_saddr=INADDR_ANY;
		sk->daddr=INADDR_ANY;
		sk->state = TCP_CLOSE;
		if(uh_cache_sk == sk)
			uh_cache_sk = NULL;
		return 0;
	}

	if (usin->sin_family && usin->sin_family != AF_INET)
	  	return(-EAFNOSUPPORT);

	dst_release(xchg(&sk->dst_cache, NULL));

	err = ip_route_connect(&rt, usin->sin_addr.s_addr, sk->saddr,
			       sk->ip_tos|sk->localroute, sk->bound_dev_if);
	if (err)
		return err;
	if ((rt->rt_flags&RTCF_BROADCAST) && !sk->broadcast) {
		ip_rt_put(rt);
		return -EACCES;
	}
  	if(!sk->saddr)
	  	sk->saddr = rt->rt_src;		/* Update source address */
	if(!sk->rcv_saddr)
		sk->rcv_saddr = rt->rt_src;
	sk->daddr = rt->rt_dst;
	sk->dport = usin->sin_port;
	sk->state = TCP_ESTABLISHED;

	if(uh_cache_sk == sk)
		uh_cache_sk = NULL;

	sk->dst_cache = &rt->u.dst;
	return(0);
}


static void udp_close(struct sock *sk, long timeout)
{
	/* See for explanation: raw_close in ipv4/raw.c */
	sk->state = TCP_CLOSE;
	udp_v4_unhash(sk);
	sk->dead = 1;
	destroy_sock(sk);
}

static int udp_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	/*
	 *	Charge it to the socket, dropping if the queue is full.
	 */

#if defined(CONFIG_FILTER) && defined(CONFIG_UDP_DELAY_CSUM)
	if (sk->filter && skb->ip_summed != CHECKSUM_UNNECESSARY) {
		if ((unsigned short)csum_fold(csum_partial(skb->h.raw, skb->len, skb->csum))) {
			udp_statistics.UdpInErrors++;
			ip_statistics.IpInDiscards++;
			ip_statistics.IpInDelivers--;
			kfree_skb(skb);
			return -1;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
#endif

	if (sock_queue_rcv_skb(sk,skb)<0) {
		udp_statistics.UdpInErrors++;
		ip_statistics.IpInDiscards++;
		ip_statistics.IpInDelivers--;
		kfree_skb(skb);
		return -1;
	}
	udp_statistics.UdpInDatagrams++;
	return 0;
}


static inline void udp_deliver(struct sock *sk, struct sk_buff *skb)
{
	udp_queue_rcv_skb(sk, skb);
}

/*
 *	Multicasts and broadcasts go to each listener.
 *
 *	Note: called only from the BH handler context,
 *	so we don't need to lock the hashes.
 */
static int udp_v4_mcast_deliver(struct sk_buff *skb, struct udphdr *uh,
				 u32 saddr, u32 daddr)
{
	struct sock *sk;
	int dif;

	sk = udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)];
	dif = skb->dev->ifindex;
	sk = udp_v4_mcast_next(sk, uh->dest, saddr, uh->source, daddr, dif);
	if (sk) {
		struct sock *sknext = NULL;

		do {
			struct sk_buff *skb1 = skb;

			sknext = udp_v4_mcast_next(sk->next, uh->dest, saddr,
						   uh->source, daddr, dif);
			if(sknext)
				skb1 = skb_clone(skb, GFP_ATOMIC);

			if(skb1)
				udp_deliver(sk, skb1);
			sk = sknext;
		} while(sknext);
	} else
		kfree_skb(skb);
	return 0;
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check whether a received UDP packet might be for one of our
 *	sockets.
 */

int udp_chkaddr(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct udphdr *uh = (struct udphdr *)(skb->nh.raw + iph->ihl*4);
	struct sock *sk;

	sk = udp_v4_lookup(iph->saddr, uh->source, iph->daddr, uh->dest, skb->dev->ifindex);
	if (!sk)
		return 0;

	/* 0 means accept all LOCAL addresses here, not all the world... */
	if (sk->rcv_saddr == 0)
		return 0;

	return 1;
}
#endif

/*
 *	All we need to do is get the socket, and then do a checksum.
 */

int udp_rcv(struct sk_buff *skb, unsigned short len)
{
  	struct sock *sk;
  	struct udphdr *uh;
	unsigned short ulen;
	struct rtable *rt = (struct rtable*)skb->dst;
	u32 saddr = skb->nh.iph->saddr;
	u32 daddr = skb->nh.iph->daddr;

	/*
	 * First time through the loop.. Do all the setup stuff
	 * (including finding out the socket we go to etc)
	 */

	/*
	 *	Get the header.
	 */

  	uh = skb->h.uh;
	__skb_pull(skb, skb->h.raw - skb->data);

  	ip_statistics.IpInDelivers++;

	/*
	 *	Validate the packet and the UDP length.
	 */

	ulen = ntohs(uh->len);

	if (ulen > len || ulen < sizeof(*uh)) {
		NETDEBUG(printk(KERN_DEBUG "UDP: short packet: %d/%d\n", ulen, len));
		udp_statistics.UdpInErrors++;
		kfree_skb(skb);
		return(0);
	}
	skb_trim(skb, ulen);

#ifndef CONFIG_UDP_DELAY_CSUM
	if (uh->check &&
	    (((skb->ip_summed==CHECKSUM_HW)&&udp_check(uh,ulen,saddr,daddr,skb->csum)) ||
	     ((skb->ip_summed==CHECKSUM_NONE) &&
	      (udp_check(uh,ulen,saddr,daddr, csum_partial((char*)uh, ulen, 0))))))
		goto csum_error;
#else
	if (uh->check==0)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else if (skb->ip_summed==CHECKSUM_HW) {
		if (udp_check(uh,ulen,saddr,daddr,skb->csum))
			goto csum_error;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = csum_tcpudp_nofold(saddr, daddr, ulen, IPPROTO_UDP, 0);
#endif

	if(rt->rt_flags & (RTCF_BROADCAST|RTCF_MULTICAST))
		return udp_v4_mcast_deliver(skb, uh, saddr, daddr);

#ifdef CONFIG_IP_TRANSPARENT_PROXY
	if (IPCB(skb)->redirport)
		sk = udp_v4_proxy_lookup(uh->dest, saddr, uh->source,
					 daddr, skb->dev, IPCB(skb)->redirport,
					 skb->dev->ifindex);
	else
#endif
	sk = udp_v4_lookup(saddr, uh->source, daddr, uh->dest, skb->dev->ifindex);

	if (sk == NULL) {
#ifdef CONFIG_UDP_DELAY_CSUM
		if (skb->ip_summed != CHECKSUM_UNNECESSARY &&
		    (unsigned short)csum_fold(csum_partial((char*)uh, ulen, skb->csum)))
			goto csum_error;
#endif
  		udp_statistics.UdpNoPorts++;
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

		/*
		 * Hmm.  We got an UDP broadcast to a port to which we
		 * don't wanna listen.  Ignore it.
		 */
		kfree_skb(skb);
		return(0);
  	}
	udp_deliver(sk, skb);
	return 0;

csum_error:
	/*
	 * RFC1122: OK.  Discards the bad packet silently (as far as
	 * the network is concerned, anyway) as per 4.1.3.4 (MUST).
	 */
	NETDEBUG(printk(KERN_DEBUG "UDP: bad checksum. From %d.%d.%d.%d:%d to %d.%d.%d.%d:%d ulen %d\n",
			NIPQUAD(saddr),
			ntohs(uh->source),
			NIPQUAD(daddr),
			ntohs(uh->dest),
			ulen));
	udp_statistics.UdpInErrors++;
	kfree_skb(skb);
	return(0);
}

struct proto udp_prot = {
	(struct sock *)&udp_prot,	/* sklist_next */
	(struct sock *)&udp_prot,	/* sklist_prev */
	udp_close,			/* close */
	udp_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	udp_ioctl,			/* ioctl */
	NULL,				/* init */
	NULL,				/* destroy */
	NULL,				/* shutdown */
	ip_setsockopt,			/* setsockopt */
	ip_getsockopt,			/* getsockopt */
	udp_sendmsg,			/* sendmsg */
	udp_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	udp_queue_rcv_skb,		/* backlog_rcv */
	udp_v4_hash,			/* hash */
	udp_v4_unhash,			/* unhash */
	udp_v4_get_port,		/* good_socknum */
	128,				/* max_header */
	0,				/* retransmits */
 	"UDP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};
