/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	$Id: raw.c,v 1.39.2.1 1999/06/20 20:14:50 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared skbuff
 *					library. No more peek crashes, no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *		Alan Cox	:	Beginnings of mrouted support.
 *		Alan Cox	:	Added IP_HDRINCL option.
 *		Alan Cox	:	Skip broadcast check if BSDism set.
 *		David S. Miller	:	New socket lookup architecture.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
#include <linux/config.h> 
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <net/checksum.h>

#ifdef CONFIG_IP_MROUTE
struct sock *mroute_socket=NULL;
#endif

struct sock *raw_v4_htable[RAWV4_HTABLE_SIZE];

static void raw_v4_hash(struct sock *sk)
{
	struct sock **skp = &raw_v4_htable[sk->num & (RAWV4_HTABLE_SIZE - 1)];

	SOCKHASH_LOCK();
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	SOCKHASH_UNLOCK();
}

static void raw_v4_unhash(struct sock *sk)
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

/* Grumble... icmp and ip_input want to get at this... */
struct sock *raw_v4_lookup(struct sock *sk, unsigned short num,
			   unsigned long raddr, unsigned long laddr, int dif)
{
	struct sock *s = sk;

	SOCKHASH_LOCK();
	for(s = sk; s; s = s->next) {
		if((s->num == num) 				&&
		   !(s->dead && (s->state == TCP_CLOSE))	&&
		   !(s->daddr && s->daddr != raddr) 		&&
		   !(s->rcv_saddr && s->rcv_saddr != laddr)	&&
		   !(s->bound_dev_if && s->bound_dev_if != dif))
			break; /* gotcha */
	}
	SOCKHASH_UNLOCK();
	return s;
}

void raw_err (struct sock *sk, struct sk_buff *skb)
{
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	u32 info = 0;
	int err = 0;
	int harderr = 0;

	/* Report error on raw socket, if:
	   1. User requested ip_recverr.
	   2. Socket is connected (otherwise the error indication
	      is useless without ip_recverr and error is hard.
	 */
	if (!sk->ip_recverr && sk->state != TCP_ESTABLISHED)
		return;

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
		err = EHOSTUNREACH;
		if (code > NR_ICMP_UNREACH)
			break;
		err = icmp_err_convert[code].errno;
		harderr = icmp_err_convert[code].fatal;
		if (code == ICMP_FRAG_NEEDED) {
			harderr = (sk->ip_pmtudisc != IP_PMTUDISC_DONT);
			err = EMSGSIZE;
			info = ntohs(skb->h.icmph->un.frag.mtu);
		}
	}

	if (sk->ip_recverr)
		ip_icmp_error(sk, skb, err, 0, info, (u8 *)(skb->h.icmph + 1));
		
	if (sk->ip_recverr || harderr) {
		sk->err = err;
		sk->error_report(sk);
	}
}

static int raw_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	
	if (sock_queue_rcv_skb(sk,skb)<0)
	{
		ip_statistics.IpInDiscards++;
		kfree_skb(skb);
		return -1;
	}

	ip_statistics.IpInDelivers++;
	return 0;
}

/*
 *	This should be the easiest of all, all we do is
 *	copy it into a buffer. All demultiplexing is done
 *	in ip.c
 */

int raw_rcv(struct sock *sk, struct sk_buff *skb)
{
	/* Now we need to copy this into memory. */
	skb_trim(skb, ntohs(skb->nh.iph->tot_len));
	
	skb->h.raw = skb->nh.raw;

	raw_rcv_skb(sk, skb);
	return 0;
}

struct rawfakehdr 
{
	struct  iovec *iov;
	u32	saddr;
};

/*
 *	Send a RAW IP packet.
 */

/*
 *	Callback support is trivial for SOCK_RAW
 */
  
static int raw_getfrag(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct rawfakehdr *rfh = (struct rawfakehdr *) p;
	return memcpy_fromiovecend(to, rfh->iov, offset, fraglen);
}

/*
 *	IPPROTO_RAW needs extra work.
 */
 
static int raw_getrawfrag(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct rawfakehdr *rfh = (struct rawfakehdr *) p;

	if (memcpy_fromiovecend(to, rfh->iov, offset, fraglen))
		return -EFAULT;

	if (offset==0) {
		struct iphdr *iph = (struct iphdr *)to;
		if (!iph->saddr)
			iph->saddr = rfh->saddr;
		iph->check=0;
		iph->tot_len=htons(fraglen);	/* This is right as you can't frag
						   RAW packets */
		/*
	 	 *	Deliberate breach of modularity to keep 
	 	 *	ip_build_xmit clean (well less messy).
		 */
		if (!iph->id)
			iph->id = htons(ip_id_count++);
		iph->check=ip_fast_csum((unsigned char *)iph, iph->ihl);
	}
	return 0;
}

static int raw_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	struct ipcm_cookie ipc;
	struct rawfakehdr rfh;
	struct rtable *rt = NULL;
	int free = 0;
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

	if (msg->msg_flags & MSG_OOB)		/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;
			 
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);

	/*
	 *	Get and verify the address. 
	 */

	if (msg->msg_namelen) {
		struct sockaddr_in *usin = (struct sockaddr_in*)msg->msg_name;
		if (msg->msg_namelen < sizeof(*usin))
			return(-EINVAL);
		if (usin->sin_family != AF_INET) {
			static int complained;
			if (!complained++)
				printk(KERN_INFO "%s forgot to set AF_INET in raw sendmsg. Fix it!\n", current->comm);
			if (usin->sin_family)
				return -EINVAL;
		}
		daddr = usin->sin_addr.s_addr;
		/* ANK: I did not forget to get protocol from port field.
		 * I just do not know, who uses this weirdness.
		 * IP_HDRINCL is much more convenient.
		 */
	} else {
		if (sk->state != TCP_ESTABLISHED) 
			return(-EINVAL);
		daddr = sk->daddr;
	}

	ipc.addr = sk->saddr;
	ipc.opt = NULL;
	ipc.oif = sk->bound_dev_if;

	if (msg->msg_controllen) {
		int tmp = ip_cmsg_send(msg, &ipc);
		if (tmp)
			return tmp;
		if (ipc.opt)
			free=1;
	}

	rfh.saddr = ipc.addr;
	ipc.addr = daddr;

	if (!ipc.opt)
		ipc.opt = sk->opt;

	if (ipc.opt) {
		err = -EINVAL;
		/* Linux does not mangle headers on raw sockets,
		 * so that IP options + IP_HDRINCL is non-sense.
		 */
		if (sk->ip_hdrincl)
			goto done;
		if (ipc.opt->srr) {
			if (!daddr)
				goto done;
			daddr = ipc.opt->faddr;
		}
	}
	tos = RT_TOS(sk->ip_tos) | sk->localroute;
	if (msg->msg_flags&MSG_DONTROUTE)
		tos |= RTO_ONLINK;

	if (MULTICAST(daddr)) {
		if (!ipc.oif)
			ipc.oif = sk->ip_mc_index;
		if (!rfh.saddr)
			rfh.saddr = sk->ip_mc_addr;
	}

	err = ip_route_output(&rt, daddr, rfh.saddr, tos, ipc.oif);

	if (err)
		goto done;

	err = -EACCES;
	if (rt->rt_flags&RTCF_BROADCAST && !sk->broadcast)
		goto done;

	rfh.iov = msg->msg_iov;
	rfh.saddr = rt->rt_src;
	if (!ipc.addr)
		ipc.addr = rt->rt_dst;
	err=ip_build_xmit(sk, sk->ip_hdrincl ? raw_getrawfrag : raw_getfrag,
			  &rfh, len, &ipc, rt, msg->msg_flags);

done:
	if (free)
		kfree(ipc.opt);
	ip_rt_put(rt);

	return err<0 ? err : len;
}

static void raw_close(struct sock *sk, long timeout)
{
	/* Observation: when raw_close is called, processes have
	   no access to socket anymore. But net still has.
	   Step one, detach it from networking:

	   A. Remove from hash tables.
	 */
	sk->state = TCP_CLOSE;
	raw_v4_unhash(sk);
        /*
	   B. Raw sockets may have direct kernel references. Kill them.
	 */
	ip_ra_control(sk, 0, NULL);

	/* In this point socket cannot receive new packets anymore */


	/* But we still have packets pending on receive
	   queue and probably, our own packets waiting in device queues.
	   sock_destroy will drain receive queue, but transmitted
	   packets will delay socket destruction.
	   Set sk->dead=1 in order to prevent wakeups, when these
	   packet will be freed.
	 */
	sk->dead=1;
	destroy_sock(sk);

	/* That's all. No races here. */
}

/* This gets rid of all the nasties in af_inet. -DaveM */
static int raw_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
	int chk_addr_ret;

	if((sk->state != TCP_CLOSE) || (addr_len < sizeof(struct sockaddr_in)))
		return -EINVAL;
	chk_addr_ret = inet_addr_type(addr->sin_addr.s_addr);
	if(addr->sin_addr.s_addr != 0 && chk_addr_ret != RTN_LOCAL &&
	   chk_addr_ret != RTN_MULTICAST && chk_addr_ret != RTN_BROADCAST) {
#ifdef CONFIG_IP_TRANSPARENT_PROXY
		/* Superuser may bind to any address to allow transparent proxying. */
		if(chk_addr_ret != RTN_UNICAST || !capable(CAP_NET_ADMIN))
#endif
			return -EADDRNOTAVAIL;
	}
	sk->rcv_saddr = sk->saddr = addr->sin_addr.s_addr;
	if(chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		sk->saddr = 0;  /* Use device */
	dst_release(xchg(&sk->dst_cache, NULL));
	return 0;
}

/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int raw_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		int noblock, int flags,int *addr_len)
{
	int copied=0;
	struct sk_buff *skb;
	int err;
	struct sockaddr_in *sin=(struct sockaddr_in *)msg->msg_name;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (addr_len)
		*addr_len=sizeof(*sin);

	if (flags & MSG_ERRQUEUE)
		return ip_recv_error(sk, msg, len);

	skb=skb_recv_datagram(sk,flags,noblock,&err);
	if(skb==NULL)
 		return err;

	copied = skb->len;
	if (len < copied)
	{
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto done;

	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (sin) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
	}
	if (sk->ip_cmsg_flags)
		ip_cmsg_recv(msg, skb);
done:
	skb_free_datagram(sk, skb);
	return (err ? : copied);
}

static int raw_init(struct sock *sk)
{
	struct raw_opt *tp = &(sk->tp_pinfo.tp_raw4);
	if (sk->num == IPPROTO_ICMP)
		memset(&tp->filter, 0, sizeof(tp->filter));
	return 0;
}

static int raw_seticmpfilter(struct sock *sk, char *optval, int optlen)
{
	if (optlen > sizeof(struct icmp_filter))
		optlen = sizeof(struct icmp_filter);
	if (copy_from_user(&sk->tp_pinfo.tp_raw4.filter, optval, optlen))
		return -EFAULT;
	return 0;
}

static int raw_geticmpfilter(struct sock *sk, char *optval, int *optlen)
{
	int len;

	if (get_user(len,optlen))
		return -EFAULT;
	if (len > sizeof(struct icmp_filter))
		len = sizeof(struct icmp_filter);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &sk->tp_pinfo.tp_raw4.filter, len))
		return -EFAULT;
	return 0;
}

static int raw_setsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int optlen)
{
	if (level != SOL_RAW)
		return ip_setsockopt(sk, level, optname, optval, optlen);

	switch (optname) {
	case ICMP_FILTER:
		if (sk->num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		return raw_seticmpfilter(sk, optval, optlen);
	};

	return -ENOPROTOOPT;
}

static int raw_getsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int *optlen)
{
	if (level != SOL_RAW)
		return ip_getsockopt(sk, level, optname, optval, optlen);

	switch (optname) {
	case ICMP_FILTER:
		if (sk->num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		return raw_geticmpfilter(sk, optval, optlen);
	};

	return -ENOPROTOOPT;
}

struct proto raw_prot = {
	(struct sock *)&raw_prot,	/* sklist_next */
	(struct sock *)&raw_prot,	/* sklist_prev */
	raw_close,			/* close */
	udp_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
#ifdef CONFIG_IP_MROUTE
	ipmr_ioctl,			/* ioctl */
#else
	NULL,				/* ioctl */
#endif
	raw_init,			/* init */
	NULL,				/* destroy */
	NULL,				/* shutdown */
	raw_setsockopt,			/* setsockopt */
	raw_getsockopt,			/* getsockopt */
	raw_sendmsg,			/* sendmsg */
	raw_recvmsg,			/* recvmsg */
	raw_bind,			/* bind */
	raw_rcv_skb,			/* backlog_rcv */
	raw_v4_hash,			/* hash */
	raw_v4_unhash,			/* unhash */
	NULL,				/* get_port */
	128,				/* max_header */
	0,				/* retransmits */
	"RAW",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};
