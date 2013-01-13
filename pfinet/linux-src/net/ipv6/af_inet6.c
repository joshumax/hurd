/*
 *	PF_INET6 socket protocol family
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Adapted from linux/net/ipv4/af_inet.c
 *
 *	$Id: af_inet6.c,v 1.3 2007/10/13 01:43:00 stesie Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/version.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/ipip.h>
#include <net/protocol.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#ifdef MODULE
static int unloadable = 0; /* XX: Turn to one when all is ok within the
			      module for allowing unload */
#endif

#if defined(MODULE) && LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Cast of dozens");
MODULE_DESCRIPTION("IPv6 protocol stack for Linux");
MODULE_PARM(unloadable, "i");
#endif

extern struct proto_ops inet6_stream_ops;
extern struct proto_ops inet6_dgram_ops;

/* IPv6 procfs goodies... */

#ifdef CONFIG_PROC_FS
extern int raw6_get_info(char *, char **, off_t, int, int);
extern int tcp6_get_info(char *, char **, off_t, int, int);
extern int udp6_get_info(char *, char **, off_t, int, int);
extern int afinet6_get_info(char *, char **, off_t, int, int);
extern int afinet6_get_snmp(char *, char **, off_t, int, int);
#endif

#ifdef CONFIG_SYSCTL
extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

int sysctl_ipv6_bindv6only;

static int inet6_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct proto *prot;

	sk = sk_alloc(PF_INET6, GFP_KERNEL, 1);
	if (sk == NULL) 
		goto do_oom;

	if(sock->type == SOCK_STREAM || sock->type == SOCK_SEQPACKET) {
		if (protocol && protocol != IPPROTO_TCP) 
			goto free_and_noproto;
		protocol = IPPROTO_TCP;
		prot = &tcpv6_prot;
		sock->ops = &inet6_stream_ops;
	} else if(sock->type == SOCK_DGRAM) {
		if (protocol && protocol != IPPROTO_UDP) 
			goto free_and_noproto;
		protocol = IPPROTO_UDP;
		sk->no_check = UDP_NO_CHECK;
		prot=&udpv6_prot;
		sock->ops = &inet6_dgram_ops;
	} else if(sock->type == SOCK_RAW) {
		if (!capable(CAP_NET_RAW))
			goto free_and_badperm;
		if (!protocol) 
			goto free_and_noproto;
		prot = &rawv6_prot;
		sock->ops = &inet6_dgram_ops;
		sk->reuse = 1;
		sk->num = protocol;
	} else {
		goto free_and_badtype;
	}
	
	sock_init_data(sock, sk);

	sk->destruct            = NULL;
	sk->zapped		= 0;
	sk->family		= PF_INET6;
	sk->protocol		= protocol;

	sk->prot		= prot;
	sk->backlog_rcv		= prot->backlog_rcv;

	sk->timer.data		= (unsigned long)sk;
	sk->timer.function	= &net_timer;

	sk->net_pinfo.af_inet6.hop_limit  = -1;
	sk->net_pinfo.af_inet6.mcast_hops = -1;
	sk->net_pinfo.af_inet6.mc_loop	  = 1;
	sk->net_pinfo.af_inet6.pmtudisc	  = IPV6_PMTUDISC_WANT;

	sk->net_pinfo.af_inet6.ipv6only	  = sysctl_ipv6_bindv6only;

	/* Init the ipv4 part of the socket since we can have sockets
	 * using v6 API for ipv4.
	 */
	sk->ip_ttl	= 64;

	sk->ip_mc_loop	= 1;
	sk->ip_mc_ttl	= 1;
	sk->ip_mc_index	= 0;
	sk->ip_mc_list	= NULL;

	if (sk->type==SOCK_RAW && protocol==IPPROTO_RAW)
		sk->ip_hdrincl=1;

	if (sk->num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically shares.
		 */
		sk->sport = ntohs(sk->num);
		sk->prot->hash(sk);
		add_to_prot_sklist(sk);
	}

	if (sk->prot->init) {
		int err = sk->prot->init(sk);
		if (err != 0) {
			destroy_sock(sk);
			return(err);
		}
	}
	MOD_INC_USE_COUNT;
	return(0);

free_and_badtype:
	sk_free(sk);
	return -ESOCKTNOSUPPORT;
free_and_badperm:
	sk_free(sk);
	return -EPERM;
free_and_noproto:
	sk_free(sk);
	return -EPROTONOSUPPORT;
do_oom:
	return -ENOBUFS;
}


/* bind for INET6 API */
static int inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6 *addr=(struct sockaddr_in6 *)uaddr;
	struct sock *sk = sock->sk;
	__u32 v4addr = 0;
	unsigned short snum;
	int addr_type = 0;

	/* If the socket has its own bind function then use it. */
	if(sk->prot->bind)
		return sk->prot->bind(sk, uaddr, addr_len);
		
	/* Check these errors (active socket, bad address length, double bind). */
	if ((sk->state != TCP_CLOSE)			||
	    (addr_len < sizeof(struct sockaddr_in6))	||
	    (sk->num != 0))
		return -EINVAL;
		
	addr_type = ipv6_addr_type(&addr->sin6_addr);
	if ((addr_type & IPV6_ADDR_MULTICAST) && sock->type == SOCK_STREAM)
		return(-EINVAL);

	/* Check if the address belongs to the host. */
	if (addr_type == IPV6_ADDR_MAPPED) {
		v4addr = addr->sin6_addr.s6_addr32[3];
		if (inet_addr_type(v4addr) != RTN_LOCAL)
			return(-EADDRNOTAVAIL);
	} else {
		if (addr_type != IPV6_ADDR_ANY) {
			struct net_device *dev = NULL;

			if (addr_type & IPV6_ADDR_LINKLOCAL) {
				if (addr_len >= sizeof(struct sockaddr_in6) &&
				    addr->sin6_scope_id) {
					/* Override any existing binding,
					   if another one is supplied
					   by user. */
					sk->bound_dev_if = addr->sin6_scope_id;
				}

				/* Binding to link-local address requires
				   an interface */
				if (!sk->bound_dev_if)
					return(-EINVAL);
				dev = dev_get_by_index(sk->bound_dev_if);
				if (!dev)
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

	snum = ntohs(addr->sin6_port);
	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return(-EACCES);

	/* Make sure we are allowed to bind here. */
	if(sk->prot->get_port(sk, snum) != 0)
		return -EADDRINUSE;

	sk->sport = ntohs(sk->num);
	sk->dport = 0;
	sk->daddr = 0;
	sk->prot->hash(sk);
	add_to_prot_sklist(sk);

	return(0);
}

static int inet6_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = sock->sk;

	if (sk == NULL)
		return -EINVAL;

	/* Free mc lists */
	ipv6_sock_mc_close(sk);

	/* Huh! MOD_DEC_USE_COUNT was here :-(
	   It is impossible by two reasons: socket destroy
	   may be delayed and inet_release may sleep and
	   return to nowhere then. It should be moved to
	   inet6_destroy_sock(), but we have no explicit constructor :-(
	                                    --ANK (980802)
	 */
	MOD_DEC_USE_COUNT;
	return inet_release(sock, peer);
}

int inet6_destroy_sock(struct sock *sk)
{
	struct sk_buff *skb;
	struct ipv6_txoptions *opt;

	/*
	 *	Release destination entry
	 */

	dst_release(xchg(&sk->dst_cache,NULL));

	/* Release rx options */

	if ((skb = xchg(&sk->net_pinfo.af_inet6.pktoptions, NULL)) != NULL)
		kfree_skb(skb);

	/* Free flowlabels */
	fl6_free_socklist(sk);

	/* Free tx options */

	if ((opt = xchg(&sk->net_pinfo.af_inet6.opt, NULL)) != NULL)
		sock_kfree_s(sk, opt, opt->tot_len);

	return 0;
}

/*
 *	This does both peername and sockname.
 */
 
static int inet6_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sockaddr_in6 *sin=(struct sockaddr_in6 *)uaddr;
	struct sock *sk;
  
	sin->sin6_family = AF_INET6;
	sin->sin6_flowinfo = 0;
	sin->sin6_scope_id = 0;

	sk = sock->sk;
	if (peer) {
		if (!tcp_connected(sk->state))
			return(-ENOTCONN);
		sin->sin6_port = sk->dport;
		memcpy(&sin->sin6_addr, &sk->net_pinfo.af_inet6.daddr,
		       sizeof(struct in6_addr));
		if (sk->net_pinfo.af_inet6.sndflow)
			sin->sin6_flowinfo = sk->net_pinfo.af_inet6.flow_label;
	} else {
		if (ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr) == IPV6_ADDR_ANY)
			memcpy(&sin->sin6_addr, 
			       &sk->net_pinfo.af_inet6.saddr,
			       sizeof(struct in6_addr));
		else
			memcpy(&sin->sin6_addr, 
			       &sk->net_pinfo.af_inet6.rcv_saddr,
			       sizeof(struct in6_addr));

		sin->sin6_port = sk->sport;
	}
	if (ipv6_addr_type(&sin->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin->sin6_scope_id = sk->bound_dev_if;
	*uaddr_len = sizeof(*sin);	
	return(0);
}

#ifdef _HURD_
#define inet6_ioctl 0
#else

static int inet6_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err = -EINVAL;
	int pid;

	switch(cmd) 
	{
	case FIOSETOWN:
	case SIOCSPGRP:
		err = get_user(pid, (int *) arg);
		if(err)
			return err;

		/* see sock_no_fcntl */
		if (current->pid != pid && current->pgrp != -pid && 
		    !capable(CAP_NET_ADMIN))
			return -EPERM;
		sk->proc = pid;
		return(0);
	case FIOGETOWN:
	case SIOCGPGRP:
		err = put_user(sk->proc,(int *)arg);
		if(err)
			return err;
		return(0);
	case SIOCGSTAMP:
		if(sk->stamp.tv_sec==0)
			return -ENOENT;
		err = copy_to_user((void *)arg, &sk->stamp,
				   sizeof(struct timeval));
		if (err)
			return -EFAULT;
		return 0;

	case SIOCADDRT:
	case SIOCDELRT:
	  
		return(ipv6_route_ioctl(cmd,(void *)arg));

	case SIOCSIFADDR:
		return addrconf_add_ifaddr((void *) arg);
	case SIOCDIFADDR:
		return addrconf_del_ifaddr((void *) arg);
	case SIOCSIFDSTADDR:
		return addrconf_set_dstaddr((void *) arg);
	default:
		if ((cmd >= SIOCDEVPRIVATE) &&
		    (cmd <= (SIOCDEVPRIVATE + 15)))
			return(dev_ioctl(cmd,(void *) arg));
		
		if(sk->prot->ioctl==0 || (err=sk->prot->ioctl(sk, cmd, arg))==-ENOIOCTLCMD)
			return(dev_ioctl(cmd,(void *) arg));		
		return err;
	}
	/*NOTREACHED*/
	return(0);
}

#endif /* not _HURD_ */

struct proto_ops inet6_stream_ops = {
	PF_INET6,

	sock_no_dup,
	inet6_release,
	inet6_bind,
	inet_stream_connect,		/* ok		*/
	sock_no_socketpair,		/* a do nothing	*/
	inet_accept,			/* ok		*/
	inet6_getname, 
	inet_poll,			/* ok		*/
	inet6_ioctl,			/* must change  */
	inet_listen,			/* ok		*/
	inet_shutdown,			/* ok		*/
	inet_setsockopt,		/* ok		*/
	inet_getsockopt,		/* ok		*/
	sock_no_fcntl,			/* ok		*/
	inet_sendmsg,			/* ok		*/
	inet_recvmsg			/* ok		*/
};

struct proto_ops inet6_dgram_ops = {
	PF_INET6,

	sock_no_dup,
	inet6_release,
	inet6_bind,
	inet_dgram_connect,		/* ok		*/
	sock_no_socketpair,		/* a do nothing	*/
	inet_accept,			/* ok		*/
	inet6_getname, 
	datagram_poll,			/* ok		*/
	inet6_ioctl,			/* must change  */
	sock_no_listen,			/* ok		*/
	inet_shutdown,			/* ok		*/
	inet_setsockopt,		/* ok		*/
	inet_getsockopt,		/* ok		*/
	sock_no_fcntl,			/* ok		*/
	inet_sendmsg,			/* ok		*/
	inet_recvmsg			/* ok		*/
};

struct net_proto_family inet6_family_ops = {
	PF_INET6,
	inet6_create
};

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_raw6 = {
	PROC_NET_RAW6, 4, "raw6",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	raw6_get_info
};
static struct proc_dir_entry proc_net_tcp6 = {
	PROC_NET_TCP6, 4, "tcp6",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	tcp6_get_info
};
static struct proc_dir_entry proc_net_udp6 = {
	PROC_NET_RAW6, 4, "udp6",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	udp6_get_info
};
static struct proc_dir_entry proc_net_sockstat6 = {
	PROC_NET_SOCKSTAT6, 9, "sockstat6",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	afinet6_get_info
};
static struct proc_dir_entry proc_net_snmp6 = {
	PROC_NET_SNMP6, 5, "snmp6",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	afinet6_get_snmp
};
#endif	/* CONFIG_PROC_FS */

#ifdef MODULE
int ipv6_unload(void)
{
	if (!unloadable) return 1;
	/* We keep internally 3 raw sockets */
	return atomic_read(&(__this_module.uc.usecount)) - 3;
}
#endif

#if defined(MODULE) && defined(CONFIG_SYSCTL)
extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

#ifdef MODULE
int init_module(void)
#else
__initfunc(void inet6_proto_init(struct net_proto *pro))
#endif
{
	struct sk_buff *dummy_skb;
	int err;

#ifdef MODULE
	if (!mod_member_present(&__this_module, can_unload))
	  return -EINVAL;

	__this_module.can_unload = &ipv6_unload;
#endif

	printk(KERN_INFO "IPv6 v0.8 for NET4.0\n");

	if (sizeof(struct inet6_skb_parm) > sizeof(dummy_skb->cb))
	{
		printk(KERN_CRIT "inet6_proto_init: size fault\n");
#ifdef MODULE
		return -EINVAL;
#else
		return;
#endif
	}

	/*
	 *	ipngwg API draft makes clear that the correct semantics
	 *	for TCP and UDP is to consider one TCP and UDP instance
	 *	in a host available by both INET and INET6 APIs and
	 *	able to communicate via both network protocols.
	 */

#if defined(MODULE) && defined(CONFIG_SYSCTL)
	ipv6_sysctl_register();
#endif
	err = icmpv6_init(&inet6_family_ops);
	if (err)
		goto icmp_fail;
	err = ndisc_init(&inet6_family_ops);
	if (err)
		goto ndisc_fail;
	err = igmp6_init(&inet6_family_ops);
	if (err)
		goto igmp_fail;
	ipv6_netdev_notif_init();
	ipv6_packet_init();
	ip6_route_init();
	ip6_flowlabel_init();
	addrconf_init();
#ifndef _HURD_
	sit_init();
#endif

	/* Init v6 transport protocols. */
	udpv6_init();
	tcpv6_init();

	/* Create /proc/foo6 entries. */
#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_raw6);
	proc_net_register(&proc_net_tcp6);
	proc_net_register(&proc_net_udp6);
	proc_net_register(&proc_net_sockstat6);
	proc_net_register(&proc_net_snmp6);
#endif

	/* Now the userspace is allowed to create INET6 sockets. */
	(void) sock_register(&inet6_family_ops);
	
#ifdef MODULE
	return 0;
#else
	return;
#endif

igmp_fail:
	ndisc_cleanup();
ndisc_fail:
	icmpv6_cleanup();
icmp_fail:
#if defined(MODULE) && defined(CONFIG_SYSCTL)
	ipv6_sysctl_unregister();
#endif
#ifdef MODULE
	return err;
#else
	return;
#endif
}

#ifdef MODULE
void cleanup_module(void)
{
	/* First of all disallow new sockets creation. */
	sock_unregister(PF_INET6);
#ifdef CONFIG_PROC_FS
	proc_net_unregister(proc_net_raw6.low_ino);
	proc_net_unregister(proc_net_tcp6.low_ino);
	proc_net_unregister(proc_net_udp6.low_ino);
	proc_net_unregister(proc_net_sockstat6.low_ino);
	proc_net_unregister(proc_net_snmp6.low_ino);
#endif
	/* Cleanup code parts. */
	sit_cleanup();
	ipv6_netdev_notif_cleanup();
	ip6_flowlabel_cleanup();
	addrconf_cleanup();
	ip6_route_cleanup();
	ipv6_packet_cleanup();
	igmp6_cleanup();
	ndisc_cleanup();
	icmpv6_cleanup();
#ifdef CONFIG_SYSCTL
	ipv6_sysctl_unregister();	
#endif
}
#endif	/* MODULE */
