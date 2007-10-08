/*
 *	Multicast support for IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: mcast.c,v 1.2 2007/10/08 21:59:10 stesie Exp $
 *
 *	Based on linux/ipv4/igmp.c and linux/ipv4/ip_sockglue.c 
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/route.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/if_inet6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>

#include <net/checksum.h>

/* Set to 3 to get tracing... */
#define MCAST_DEBUG 2

#if MCAST_DEBUG >= 3
#define MDBG(x) printk x
#else
#define MDBG(x)
#endif

static struct socket *igmp6_socket;

static void igmp6_join_group(struct ifmcaddr6 *ma);
static void igmp6_leave_group(struct ifmcaddr6 *ma);
void igmp6_timer_handler(unsigned long data);

#define IGMP6_UNSOLICITED_IVAL	(10*HZ)

/*
 *	Hash list of configured multicast addresses
 */
static struct ifmcaddr6		*inet6_mcast_lst[IN6_ADDR_HSIZE];

/*
 *	socket join on multicast group
 */

int ipv6_sock_mc_join(struct sock *sk, int ifindex, struct in6_addr *addr)
{
	struct device *dev = NULL;
	struct ipv6_mc_socklist *mc_lst;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	int err;

	if (!(ipv6_addr_type(addr) & IPV6_ADDR_MULTICAST))
		return -EINVAL;

	mc_lst = sock_kmalloc(sk, sizeof(struct ipv6_mc_socklist), GFP_KERNEL);

	if (mc_lst == NULL)
		return -ENOMEM;

	mc_lst->next = NULL;
	memcpy(&mc_lst->addr, addr, sizeof(struct in6_addr));
	mc_lst->ifindex = ifindex;

	if (ifindex == 0) {
		struct rt6_info *rt;
		rt = rt6_lookup(addr, NULL, 0, 0);
		if (rt) {
			dev = rt->rt6i_dev;
			dst_release(&rt->u.dst);
		}
	} else
		dev = dev_get_by_index(ifindex);

	if (dev == NULL) {
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return -ENODEV;
	}

	/*
	 *	now add/increase the group membership on the device
	 */

	err = ipv6_dev_mc_inc(dev, addr);

	if (err) {
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
		return err;
	}

	mc_lst->next = np->ipv6_mc_list;
	np->ipv6_mc_list = mc_lst;

	return 0;
}

/*
 *	socket leave on multicast group
 */
int ipv6_sock_mc_drop(struct sock *sk, int ifindex, struct in6_addr *addr)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_mc_socklist *mc_lst, **lnk;

	for (lnk = &np->ipv6_mc_list; (mc_lst = *lnk) !=NULL ; lnk = &mc_lst->next) {
		if (mc_lst->ifindex == ifindex &&
		    ipv6_addr_cmp(&mc_lst->addr, addr) == 0) {
			struct device *dev;

			*lnk = mc_lst->next;
			synchronize_bh();

			if ((dev = dev_get_by_index(ifindex)) != NULL)
				ipv6_dev_mc_dec(dev, &mc_lst->addr);
			sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
			return 0;
		}
	}

	return -ENOENT;
}

void ipv6_sock_mc_close(struct sock *sk)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct ipv6_mc_socklist *mc_lst;

	while ((mc_lst = np->ipv6_mc_list) != NULL) {
		struct device *dev = dev_get_by_index(mc_lst->ifindex);

		if (dev)
			ipv6_dev_mc_dec(dev, &mc_lst->addr);

		np->ipv6_mc_list = mc_lst->next;
		sock_kfree_s(sk, mc_lst, sizeof(*mc_lst));
	}
}

static int igmp6_group_added(struct ifmcaddr6 *mc)
{
	char buf[MAX_ADDR_LEN];

	if (!(mc->mca_flags&MAF_LOADED)) {
		mc->mca_flags |= MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, mc->dev, 0) == 0)
			dev_mc_add(mc->dev, buf, mc->dev->addr_len, 0);
	}

	if (mc->dev->flags&IFF_UP)
		igmp6_join_group(mc);
	return 0;
}

static int igmp6_group_dropped(struct ifmcaddr6 *mc)
{
	char buf[MAX_ADDR_LEN];

	if (mc->mca_flags&MAF_LOADED) {
		mc->mca_flags &= ~MAF_LOADED;
		if (ndisc_mc_map(&mc->mca_addr, buf, mc->dev, 0) == 0)
			dev_mc_delete(mc->dev, buf, mc->dev->addr_len, 0);
	}

	if (mc->dev->flags&IFF_UP)
		igmp6_leave_group(mc);
	return 0;
}


/*
 *	device multicast group inc (add if not found)
 */
int ipv6_dev_mc_inc(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *mc;
	struct inet6_dev    *idev;
	int hash;

	idev = ipv6_get_idev(dev);

	if (idev == NULL)
		return -EINVAL;

	hash = ipv6_addr_hash(addr);

	for (mc = inet6_mcast_lst[hash]; mc; mc = mc->next) {
		if (ipv6_addr_cmp(&mc->mca_addr, addr) == 0 && mc->dev == dev) {
			atomic_inc(&mc->mca_users);
			return 0;
		}
	}

	/*
	 *	not found: create a new one.
	 */

	mc = kmalloc(sizeof(struct ifmcaddr6), GFP_ATOMIC);

	if (mc == NULL)
		return -ENOMEM;

	memset(mc, 0, sizeof(struct ifmcaddr6));
	mc->mca_timer.function = igmp6_timer_handler;
	mc->mca_timer.data = (unsigned long) mc;

	memcpy(&mc->mca_addr, addr, sizeof(struct in6_addr));
	mc->dev = dev;
	atomic_set(&mc->mca_users, 1);

	mc->next = inet6_mcast_lst[hash];
	inet6_mcast_lst[hash] = mc;

	mc->if_next = idev->mc_list;
	idev->mc_list = mc;

	igmp6_group_added(mc);

	return 0;
}

static void ipv6_mca_remove(struct device *dev, struct ifmcaddr6 *ma)
{
	struct inet6_dev *idev;

	idev = ipv6_get_idev(dev);

	if (idev) {
		struct ifmcaddr6 *iter, **lnk;
		
		for (lnk = &idev->mc_list; (iter = *lnk) != NULL; lnk = &iter->if_next) {
			if (iter == ma) {
				*lnk = iter->if_next;
				synchronize_bh();
				return;
			}
		}
	}
}

/*
 *	device multicast group del
 */
int ipv6_dev_mc_dec(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *ma, **lnk;
	int hash;

	hash = ipv6_addr_hash(addr);

	for (lnk = &inet6_mcast_lst[hash]; (ma=*lnk) != NULL; lnk = &ma->next) {
		if (ipv6_addr_cmp(&ma->mca_addr, addr) == 0 && ma->dev == dev) {
			if (atomic_dec_and_test(&ma->mca_users)) {
				igmp6_group_dropped(ma);

				*lnk = ma->next;
				synchronize_bh();

				ipv6_mca_remove(dev, ma);
				kfree(ma);
			}
			return 0;
		}
	}

	return -ENOENT;
}

/*
 *	check if the interface/address pair is valid
 */
int ipv6_chk_mcast_addr(struct device *dev, struct in6_addr *addr)
{
	struct ifmcaddr6 *mc;
	int hash;

	hash = ipv6_addr_hash(addr);

	for (mc = inet6_mcast_lst[hash]; mc; mc=mc->next) {
		if (mc->dev == dev && ipv6_addr_cmp(&mc->mca_addr, addr) == 0)
			return 1;
	}

	return 0;
}

/*
 *	IGMP handling (alias multicast ICMPv6 messages)
 */

static void igmp6_group_queried(struct ifmcaddr6 *ma, unsigned long resptime)
{
	unsigned long delay = resptime;

	/* Do not start timer for addresses with link/host scope */
	if (ipv6_addr_type(&ma->mca_addr)&(IPV6_ADDR_LINKLOCAL|IPV6_ADDR_LOOPBACK))
		return;

	if (del_timer(&ma->mca_timer))
		delay = ma->mca_timer.expires - jiffies;

	if (delay >= resptime) {
		if (resptime)
			delay = net_random() % resptime;
		else
			delay = 1;
	}

	ma->mca_flags |= MAF_TIMER_RUNNING;
	ma->mca_timer.expires = jiffies + delay;
	add_timer(&ma->mca_timer);
}

int igmp6_event_query(struct sk_buff *skb, struct icmp6hdr *hdr, int len)
{
	struct ifmcaddr6 *ma;
	struct in6_addr *addrp;
	unsigned long resptime;

	if (len < sizeof(struct icmp6hdr) + sizeof(struct in6_addr))
		return -EINVAL;

	/* Drop queries with not link local source */
	if (!(ipv6_addr_type(&skb->nh.ipv6h->saddr)&IPV6_ADDR_LINKLOCAL))
		return -EINVAL;

	resptime = ntohs(hdr->icmp6_maxdelay);
	/* Translate milliseconds to jiffies */
	resptime = (resptime<<10)/(1024000/HZ);

	addrp = (struct in6_addr *) (hdr + 1);

	if (ipv6_addr_any(addrp)) {
		struct inet6_dev *idev;

		idev = ipv6_get_idev(skb->dev);

		if (idev == NULL)
			return 0;

		for (ma = idev->mc_list; ma; ma=ma->if_next)
			igmp6_group_queried(ma, resptime);
	} else {
		int hash = ipv6_addr_hash(addrp);

		for (ma = inet6_mcast_lst[hash]; ma; ma=ma->next) {
			if (ma->dev == skb->dev &&
			    ipv6_addr_cmp(addrp, &ma->mca_addr) == 0) {
				igmp6_group_queried(ma, resptime);
				break;
			}
		}
	}

	return 0;
}


int igmp6_event_report(struct sk_buff *skb, struct icmp6hdr *hdr, int len)
{
	struct ifmcaddr6 *ma;
	struct in6_addr *addrp;
	struct device *dev;
	int hash;

	/* Our own report looped back. Ignore it. */
	if (skb->pkt_type == PACKET_LOOPBACK)
		return 0;

	if (len < sizeof(struct icmp6hdr) + sizeof(struct in6_addr))
		return -EINVAL;

	/* Drop reports with not link local source */
	if (!(ipv6_addr_type(&skb->nh.ipv6h->saddr)&IPV6_ADDR_LINKLOCAL))
		return -EINVAL;

	addrp = (struct in6_addr *) (hdr + 1);

	dev = skb->dev;

	/*
	 *	Cancel the timer for this group
	 */

	hash = ipv6_addr_hash(addrp);

	for (ma = inet6_mcast_lst[hash]; ma; ma=ma->next) {
		if ((ma->dev == dev) && ipv6_addr_cmp(&ma->mca_addr, addrp) == 0) {
			if (ma->mca_flags & MAF_TIMER_RUNNING) {
				del_timer(&ma->mca_timer);
				ma->mca_flags &= ~MAF_TIMER_RUNNING;
			}

			ma->mca_flags &= ~MAF_LAST_REPORTER;
			break;
		}
	}

	return 0;
}

void igmp6_send(struct in6_addr *addr, struct device *dev, int type)
{
	struct sock *sk = igmp6_socket->sk;
        struct sk_buff *skb;
        struct icmp6hdr *hdr;
	struct inet6_ifaddr *ifp;
	struct in6_addr *snd_addr;
	struct in6_addr *addrp;
	struct in6_addr all_routers;
	int err, len, payload_len, full_len;
	u8 ra[8] = { IPPROTO_ICMPV6, 0,
		     IPV6_TLV_ROUTERALERT, 0, 0, 0,
		     IPV6_TLV_PADN, 0 };

	snd_addr = addr;
	if (type == ICMPV6_MGM_REDUCTION) {
		snd_addr = &all_routers;
		ipv6_addr_all_routers(&all_routers);
	}

	len = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	payload_len = len + sizeof(ra);
	full_len = sizeof(struct ipv6hdr) + payload_len;

	skb = sock_alloc_send_skb(sk, dev->hard_header_len + full_len + 15, 0, 0, &err);

	if (skb == NULL)
		return;

	skb_reserve(skb, (dev->hard_header_len + 15) & ~15);
	if (dev->hard_header) {
		unsigned char ha[MAX_ADDR_LEN];
		ndisc_mc_map(snd_addr, ha, dev, 1);
		dev->hard_header(skb, dev, ETH_P_IPV6, ha, NULL, full_len);
	}

	ifp = ipv6_get_lladdr(dev);

	if (ifp == NULL) {
#if MCAST_DEBUG >= 1
		printk(KERN_DEBUG "igmp6: %s no linklocal address\n",
		       dev->name);
#endif
		return;
	}

	ip6_nd_hdr(sk, skb, dev, &ifp->addr, snd_addr, NEXTHDR_HOP, payload_len);

	memcpy(skb_put(skb, sizeof(ra)), ra, sizeof(ra));

	hdr = (struct icmp6hdr *) skb_put(skb, sizeof(struct icmp6hdr));
	memset(hdr, 0, sizeof(struct icmp6hdr));
	hdr->icmp6_type = type;

	addrp = (struct in6_addr *) skb_put(skb, sizeof(struct in6_addr));
	ipv6_addr_copy(addrp, addr);

	hdr->icmp6_cksum = csum_ipv6_magic(&ifp->addr, snd_addr, len,
					   IPPROTO_ICMPV6,
					   csum_partial((__u8 *) hdr, len, 0));

	dev_queue_xmit(skb);
	if (type == ICMPV6_MGM_REDUCTION)
		icmpv6_statistics.Icmp6OutGroupMembReductions++;
	else
		icmpv6_statistics.Icmp6OutGroupMembResponses++;
	icmpv6_statistics.Icmp6OutMsgs++;
}

static void igmp6_join_group(struct ifmcaddr6 *ma)
{
	unsigned long delay;
	int addr_type;

	addr_type = ipv6_addr_type(&ma->mca_addr);

	if ((addr_type & (IPV6_ADDR_LINKLOCAL|IPV6_ADDR_LOOPBACK)))
		return;

	start_bh_atomic();
	igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REPORT);

	delay = net_random() % IGMP6_UNSOLICITED_IVAL;
	if (del_timer(&ma->mca_timer))
		delay = ma->mca_timer.expires - jiffies;

	ma->mca_timer.expires = jiffies + delay;

	add_timer(&ma->mca_timer);
	ma->mca_flags |= MAF_TIMER_RUNNING | MAF_LAST_REPORTER;
	end_bh_atomic();
}

static void igmp6_leave_group(struct ifmcaddr6 *ma)
{
	int addr_type;

	addr_type = ipv6_addr_type(&ma->mca_addr);

	if ((addr_type & IPV6_ADDR_LINKLOCAL))
		return;

	start_bh_atomic();
	if (ma->mca_flags & MAF_LAST_REPORTER)
		igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REDUCTION);

	if (ma->mca_flags & MAF_TIMER_RUNNING)
		del_timer(&ma->mca_timer);
	end_bh_atomic();
}

void igmp6_timer_handler(unsigned long data)
{
	struct ifmcaddr6 *ma = (struct ifmcaddr6 *) data;

	ma->mca_flags |=  MAF_LAST_REPORTER;
	igmp6_send(&ma->mca_addr, ma->dev, ICMPV6_MGM_REPORT);
	ma->mca_flags &= ~MAF_TIMER_RUNNING;
}

/* Device going down */

void ipv6_mc_down(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;
	struct in6_addr maddr;

	/* Withdraw multicast list */

	for (i = idev->mc_list; i; i=i->if_next)
		igmp6_group_dropped(i);

	/* Delete all-nodes address. */

	ipv6_addr_all_nodes(&maddr);
	ipv6_dev_mc_dec(idev->dev, &maddr);
}

/* Device going up */

void ipv6_mc_up(struct inet6_dev *idev)
{
	struct ifmcaddr6 *i;
	struct in6_addr maddr;

	/* Add all-nodes address. */

	ipv6_addr_all_nodes(&maddr);
	ipv6_dev_mc_inc(idev->dev, &maddr);

	/* Install multicast list, except for all-nodes (already installed) */

	for (i = idev->mc_list; i; i=i->if_next)
		igmp6_group_added(i);
}

/*
 *	Device is about to be destroyed: clean up.
 */

void ipv6_mc_destroy_dev(struct inet6_dev *idev)
{
	int hash;
	struct ifmcaddr6 *i, **lnk;

	while ((i = idev->mc_list) != NULL) {
		idev->mc_list = i->if_next;

		hash = ipv6_addr_hash(&i->mca_addr);

		for (lnk = &inet6_mcast_lst[hash]; *lnk; lnk = &(*lnk)->next) {
			if (*lnk == i) {
				*lnk = i->next;
				synchronize_bh();
				break;
			}
		}
		igmp6_group_dropped(i);
		kfree(i);
	}
}

#ifdef CONFIG_PROC_FS
static int igmp6_read_proc(char *buffer, char **start, off_t offset,
			   int length, int *eof, void *data)
{
	off_t pos=0, begin=0;
	struct ifmcaddr6 *im;
	int len=0;
	struct device *dev;
	
	for (dev = dev_base; dev; dev = dev->next) {
		struct inet6_dev *idev;

		if ((idev = ipv6_get_idev(dev)) == NULL)
			continue;

		for (im = idev->mc_list; im; im = im->if_next) {
			int i;

			len += sprintf(buffer+len,"%-4d %-15s ", dev->ifindex, dev->name);

			for (i=0; i<16; i++)
				len += sprintf(buffer+len, "%02x", im->mca_addr.s6_addr[i]);

			len+=sprintf(buffer+len,
				     " %5d %08X %ld\n",
				     atomic_read(&im->mca_users),
				     im->mca_flags,
				     (im->mca_flags&MAF_TIMER_RUNNING) ? im->mca_timer.expires-jiffies : 0);

			pos=begin+len;
			if (pos < offset) {
				len=0;
				begin=pos;
			}
			if (pos > offset+length)
				goto done;
		}
	}
	*eof = 1;

done:
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	if (len<0)
		len=0;
	return len;
}
#endif

__initfunc(int igmp6_init(struct net_proto_family *ops))
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *ent;
#endif
	struct sock *sk;
	int err;

	igmp6_socket = sock_alloc();
	if (igmp6_socket == NULL) {
		printk(KERN_ERR
		       "Failed to create the IGMP6 control socket.\n");
		return -1;
	}
#ifndef _HURD_
	igmp6_socket->inode->i_uid = 0;
	igmp6_socket->inode->i_gid = 0;
#endif
	igmp6_socket->type = SOCK_RAW;

	if((err = ops->create(igmp6_socket, IPPROTO_ICMPV6)) < 0) {
		printk(KERN_DEBUG 
		       "Failed to initialize the IGMP6 control socket (err %d).\n",
		       err);
		sock_release(igmp6_socket);
		igmp6_socket = NULL; /* For safety. */
		return err;
	}

	sk = igmp6_socket->sk;
	sk->allocation = GFP_ATOMIC;
	sk->num = 256;			/* Don't receive any data */

	sk->net_pinfo.af_inet6.hop_limit = 1;
#ifdef CONFIG_PROC_FS
	ent = create_proc_entry("net/igmp6", 0, 0);
	ent->read_proc = igmp6_read_proc;
#endif

	return 0;
}

#ifdef MODULE
void igmp6_cleanup(void)
{
	sock_release(igmp6_socket);
	igmp6_socket = NULL; /* for safety */
#ifdef CONFIG_PROC_FS
	remove_proc_entry("net/igmp6", 0);
#endif
}
#endif
