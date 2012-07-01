/*
 *	IPv6 Address [auto]configuration
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: addrconf.c,v 1.4 2009/02/24 01:21:14 sthibaul Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Janos Farkas			:	delete timer on ifdown
 *	<chexum@bankinf.banki.hu>
 *	Andi Kleen			:	kill doube kfree on module
 *						unload.
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
#include <linux/route.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <linux/delay.h>

#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <linux/if_tunnel.h>
#include <linux/rtnetlink.h>

#include <asm/uaccess.h>

/* Set to 3 to get tracing... */
#define ACONF_DEBUG 2

#if ACONF_DEBUG >= 3
#define ADBG(x) printk x
#else
#define ADBG(x)
#endif

#ifdef CONFIG_SYSCTL
static void addrconf_sysctl_register(struct inet6_dev *idev, struct ipv6_devconf *p);
static void addrconf_sysctl_unregister(struct ipv6_devconf *p);
#endif

/*
 *	Configured unicast address list
 */
static struct inet6_ifaddr		*inet6_addr_lst[IN6_ADDR_HSIZE];

/*
 *	AF_INET6 device list
 */
static struct inet6_dev		*inet6_dev_lst[IN6_ADDR_HSIZE];

static atomic_t			addr_list_lock = ATOMIC_INIT(0);

void addrconf_verify(unsigned long);

static struct timer_list addr_chk_timer = {
	NULL, NULL,
	0, 0, addrconf_verify
};

/* These locks protect only against address deletions,
   but not against address adds or status updates.
   It is OK. The only race is when address is selected,
   which becomes invalid immediately after selection.
   It is harmless, because this address could be already invalid
   several usecs ago.

   Its important, that:

   1. The result of inet6_add_addr() is used only inside lock
      or from bh_atomic context.

   2. inet6_get_lladdr() is used only from bh protected context.

   3. The result of ipv6_chk_addr() is not used outside of bh protected context.
 */

static __inline__ void addrconf_lock(void)
{
	atomic_inc(&addr_list_lock);
	synchronize_bh();
}

static __inline__ void addrconf_unlock(void)
{
	atomic_dec(&addr_list_lock);
}

static int addrconf_ifdown(struct device *dev, int how);

static void addrconf_dad_start(struct inet6_ifaddr *ifp);
static void addrconf_dad_timer(unsigned long data);
static void addrconf_dad_completed(struct inet6_ifaddr *ifp);
static void addrconf_rs_timer(unsigned long data);
static void ipv6_ifa_notify(int event, struct inet6_ifaddr *ifa);

struct ipv6_devconf ipv6_devconf =
{
	0,				/* forwarding		*/
	IPV6_DEFAULT_HOPLIMIT,		/* hop limit		*/
	IPV6_MIN_MTU,			/* mtu			*/
	1,				/* accept RAs		*/
	1,				/* accept redirects	*/
	1,				/* autoconfiguration	*/
	1,				/* dad transmits	*/
	MAX_RTR_SOLICITATIONS,		/* router solicits	*/
	RTR_SOLICITATION_INTERVAL,	/* rtr solicit interval	*/
	MAX_RTR_SOLICITATION_DELAY,	/* rtr solicit delay	*/
};

static struct ipv6_devconf ipv6_devconf_dflt =
{
	0,				/* forwarding		*/
	IPV6_DEFAULT_HOPLIMIT,		/* hop limit		*/
	IPV6_MIN_MTU,			/* mtu			*/
	1,				/* accept RAs		*/
	1,				/* accept redirects	*/
	1,				/* autoconfiguration	*/
	1,				/* dad transmits	*/
	MAX_RTR_SOLICITATIONS,		/* router solicits	*/
	RTR_SOLICITATION_INTERVAL,	/* rtr solicit interval	*/
	MAX_RTR_SOLICITATION_DELAY,	/* rtr solicit delay	*/
};

int ipv6_addr_type(struct in6_addr *addr)
{
	u32 st;

	st = addr->s6_addr32[0];

	/* Consider all addresses with the first three bits different of
	   000 and 111 as unicasts.
	 */
	if ((st & __constant_htonl(0xE0000000)) != __constant_htonl(0x00000000) &&
	    (st & __constant_htonl(0xE0000000)) != __constant_htonl(0xE0000000))
		return IPV6_ADDR_UNICAST;

	if ((st & __constant_htonl(0xFF000000)) == __constant_htonl(0xFF000000)) {
		int type = IPV6_ADDR_MULTICAST;

		switch((st & __constant_htonl(0x00FF0000))) {
			case __constant_htonl(0x00010000):
				type |= IPV6_ADDR_LOOPBACK;
				break;

			case __constant_htonl(0x00020000):
				type |= IPV6_ADDR_LINKLOCAL;
				break;

			case __constant_htonl(0x00050000):
				type |= IPV6_ADDR_SITELOCAL;
				break;
		};
		return type;
	}
	
	if ((st & __constant_htonl(0xFFC00000)) == __constant_htonl(0xFE800000))
		return (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_UNICAST);

	if ((st & __constant_htonl(0xFFC00000)) == __constant_htonl(0xFEC00000))
		return (IPV6_ADDR_SITELOCAL | IPV6_ADDR_UNICAST);

	if ((addr->s6_addr32[0] | addr->s6_addr32[1]) == 0) {
		if (addr->s6_addr32[2] == 0) {
			if (addr->__in6_u.__u6_addr32[3] == 0)
				return IPV6_ADDR_ANY;

			if (addr->s6_addr32[3] == __constant_htonl(0x00000001))
				return (IPV6_ADDR_LOOPBACK | IPV6_ADDR_UNICAST);

			return (IPV6_ADDR_COMPATv4 | IPV6_ADDR_UNICAST);
		}

		if (addr->s6_addr32[2] == __constant_htonl(0x0000ffff))
			return IPV6_ADDR_MAPPED;
	}

	return IPV6_ADDR_RESERVED;
}

static struct inet6_dev * ipv6_add_dev(struct device *dev)
{
	struct inet6_dev *ndev, **bptr, *iter;
	int hash;

	if (dev->mtu < IPV6_MIN_MTU)
		return NULL;

	ndev = kmalloc(sizeof(struct inet6_dev), GFP_KERNEL);

	if (ndev) {
		memset(ndev, 0, sizeof(struct inet6_dev));

		ndev->dev = dev;
		memcpy(&ndev->cnf, &ipv6_devconf_dflt, sizeof(ndev->cnf));
		ndev->cnf.mtu6 = dev->mtu;
		ndev->cnf.sysctl = NULL;
		ndev->nd_parms = neigh_parms_alloc(dev, &nd_tbl);
		if (ndev->nd_parms == NULL) {
			kfree(ndev);
			return NULL;
		}
#ifdef CONFIG_SYSCTL
		neigh_sysctl_register(dev, ndev->nd_parms, NET_IPV6, NET_IPV6_NEIGH, "ipv6");
		addrconf_sysctl_register(ndev, &ndev->cnf);
#endif
		hash = ipv6_devindex_hash(dev->ifindex);
		bptr = &inet6_dev_lst[hash];
		iter = *bptr;

		for (; iter; iter = iter->next)
			bptr = &iter->next;

		*bptr = ndev;

	}
	return ndev;
}

#ifndef _HURD_
static 
#endif
struct inet6_dev * ipv6_find_idev(struct device *dev)
{
	struct inet6_dev *idev;

	if ((idev = ipv6_get_idev(dev)) == NULL) {
		idev = ipv6_add_dev(dev);
		if (idev == NULL)
			return NULL;
		if (dev->flags&IFF_UP)
			ipv6_mc_up(idev);
	}
	return idev;
}

static void addrconf_forward_change(struct inet6_dev *idev)
{
	int i;

	if (idev)
		return;

	for (i = 0; i < IN6_ADDR_HSIZE; i++) {
		for (idev = inet6_dev_lst[i]; idev; idev = idev->next)
			idev->cnf.forwarding = ipv6_devconf.forwarding;
	}
}

struct inet6_dev * ipv6_get_idev(struct device *dev)
{
	struct inet6_dev *idev;
	int hash;

	hash = ipv6_devindex_hash(dev->ifindex);

	for (idev = inet6_dev_lst[hash]; idev; idev = idev->next) {
		if (idev->dev == dev)
			return idev;
	}
	return NULL;
}

static struct inet6_ifaddr *
ipv6_add_addr(struct inet6_dev *idev, struct in6_addr *addr, int scope)
{
	struct inet6_ifaddr *ifa;
	int hash;

	ifa = kmalloc(sizeof(struct inet6_ifaddr), GFP_ATOMIC);

	if (ifa == NULL) {
		ADBG(("ipv6_add_addr: malloc failed\n"));
		return NULL;
	}

	memset(ifa, 0, sizeof(struct inet6_ifaddr));
	memcpy(&ifa->addr, addr, sizeof(struct in6_addr));

	init_timer(&ifa->timer);
	ifa->timer.data = (unsigned long) ifa;
	ifa->scope = scope;
	ifa->idev = idev;

	/* Add to list. */
	hash = ipv6_addr_hash(addr);

	ifa->lst_next = inet6_addr_lst[hash];
	inet6_addr_lst[hash] = ifa;

	/* Add to inet6_dev unicast addr list. */
	ifa->if_next = idev->addr_list;
	idev->addr_list = ifa;

	return ifa;
}

static void ipv6_del_addr(struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *iter, **back;
	int hash;

	if (atomic_read(&addr_list_lock)) {
		ifp->flags |= ADDR_INVALID;
		ipv6_ifa_notify(RTM_DELADDR, ifp);
		return;
	}

	hash = ipv6_addr_hash(&ifp->addr);

	iter = inet6_addr_lst[hash];
	back = &inet6_addr_lst[hash];

	for (; iter; iter = iter->lst_next) {
		if (iter == ifp) {
			*back = ifp->lst_next;
			synchronize_bh();

			ifp->lst_next = NULL;
			break;
		}
		back = &(iter->lst_next);
	}

	iter = ifp->idev->addr_list;
	back = &ifp->idev->addr_list;

	for (; iter; iter = iter->if_next) {
		if (iter == ifp) {
			*back = ifp->if_next;
			synchronize_bh();

			ifp->if_next = NULL;
			break;
		}
		back = &(iter->if_next);
	}

	ipv6_ifa_notify(RTM_DELADDR, ifp);
	del_timer(&ifp->timer);
	
	kfree(ifp);
}

/*
 *	Choose an appropriate source address
 *	should do:
 *	i)	get an address with an appropriate scope
 *	ii)	see if there is a specific route for the destination and use
 *		an address of the attached interface 
 *	iii)	don't use deprecated addresses
 */
int ipv6_get_saddr(struct dst_entry *dst,
		   struct in6_addr *daddr, struct in6_addr *saddr)
{
	int scope;
	struct inet6_ifaddr *ifp = NULL;
	struct inet6_ifaddr *match = NULL;
	struct device *dev = NULL;
	struct rt6_info *rt;
	int err;
	int i;

	rt = (struct rt6_info *) dst;
	if (rt)
		dev = rt->rt6i_dev;
	
	addrconf_lock();

	scope = ipv6_addr_scope(daddr);
	if (rt && (rt->rt6i_flags & RTF_ALLONLINK)) {
		/*
		 *	route for the "all destinations on link" rule
		 *	when no routers are present
		 */
		scope = IFA_LINK;
	}

	/*
	 *	known dev
	 *	search dev and walk through dev addresses
	 */

	if (dev) {
		struct inet6_dev *idev;
		int hash;

		if (dev->flags & IFF_LOOPBACK)
			scope = IFA_HOST;

		hash = ipv6_devindex_hash(dev->ifindex);
		for (idev = inet6_dev_lst[hash]; idev; idev=idev->next) {
			if (idev->dev == dev) {
				for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
					if (ifp->scope == scope) {
						if (!(ifp->flags & (ADDR_STATUS|DAD_STATUS)))
							goto out;

						if (!(ifp->flags & (ADDR_INVALID|DAD_STATUS)))
							match = ifp;
					}
				}
				break;
			}
		}
	}

	if (scope == IFA_LINK)
		goto out;

	/*
	 *	dev == NULL or search failed for specified dev
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next) {
			if (ifp->scope == scope) {
				if (!(ifp->flags & (ADDR_STATUS|DAD_STATUS)))
					goto out;

				if (!(ifp->flags & (ADDR_INVALID|DAD_STATUS)))
					match = ifp;
			}
		}
	}

out:
	if (ifp == NULL)
		ifp = match;

	err = -ENETUNREACH;
	if (ifp) {
		memcpy(saddr, &ifp->addr, sizeof(struct in6_addr));
		err = 0;
	}
	addrconf_unlock();
	return err;
}

struct inet6_ifaddr * ipv6_get_lladdr(struct device *dev)
{
	struct inet6_ifaddr *ifp = NULL;
	struct inet6_dev *idev;

	if ((idev = ipv6_get_idev(dev)) != NULL) {
		addrconf_lock();
		for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
			if (ifp->scope == IFA_LINK)
				break;
		}
		addrconf_unlock();
	}
	return ifp;
}

/*
 *	Retrieve the ifaddr struct from an v6 address
 *	Called from ipv6_rcv to check if the address belongs 
 *	to the host.
 */

struct inet6_ifaddr * ipv6_chk_addr(struct in6_addr *addr, struct device *dev, int nd)
{
	struct inet6_ifaddr * ifp;
	u8 hash;
	unsigned flags = 0;

	if (!nd)
		flags |= DAD_STATUS|ADDR_INVALID;

	addrconf_lock();

	hash = ipv6_addr_hash(addr);
	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next) {
		if (ipv6_addr_cmp(&ifp->addr, addr) == 0 && !(ifp->flags&flags)) {
			if (dev == NULL || ifp->idev->dev == dev ||
			    !(ifp->scope&(IFA_LINK|IFA_HOST)))
				break;
		}
	}

	addrconf_unlock();
	return ifp;
}

void addrconf_dad_failure(struct inet6_ifaddr *ifp)
{
	printk(KERN_INFO "%s: duplicate address detected!\n", ifp->idev->dev->name);
	del_timer(&ifp->timer);
	ipv6_del_addr(ifp);
}


/* Join to solicited addr multicast group. */

static void addrconf_join_solict(struct device *dev, struct in6_addr *addr)
{
	struct in6_addr maddr;

	if (dev->flags&(IFF_LOOPBACK|IFF_NOARP))
		return;

#ifndef CONFIG_IPV6_NO_PB
	addrconf_addr_solict_mult_old(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
#endif
#ifdef CONFIG_IPV6_EUI64
	addrconf_addr_solict_mult_new(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
#endif
}

static void addrconf_leave_solict(struct device *dev, struct in6_addr *addr)
{
	struct in6_addr maddr;

	if (dev->flags&(IFF_LOOPBACK|IFF_NOARP))
		return;

#ifndef CONFIG_IPV6_NO_PB
	addrconf_addr_solict_mult_old(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
#endif
#ifdef CONFIG_IPV6_EUI64
	addrconf_addr_solict_mult_new(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
#endif
}


#ifdef CONFIG_IPV6_EUI64
static int ipv6_generate_eui64(u8 *eui, struct device *dev)
{
	switch (dev->type) {
	case ARPHRD_ETHER:
		if (dev->addr_len != ETH_ALEN)
			return -1;
		memcpy(eui, dev->dev_addr, 3);
		memcpy(eui + 5, dev->dev_addr+3, 3);
		eui[3] = 0xFF;
		eui[4] = 0xFE;
		eui[0] ^= 2;
		return 0;
	}
	return -1;
}

static int ipv6_inherit_eui64(u8 *eui, struct inet6_dev *idev)
{
	int err = -1;
	struct inet6_ifaddr *ifp;

	for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
		if (ifp->scope == IFA_LINK && !(ifp->flags&(ADDR_STATUS|DAD_STATUS))) {
			memcpy(eui, ifp->addr.s6_addr+8, 8);
			err = 0;
			break;
		}
	}
	return err;
}
#endif

/*
 *	Add prefix route.
 */

static void
addrconf_prefix_route(struct in6_addr *pfx, int plen, struct device *dev,
		      unsigned long expires, unsigned flags)
{
	struct in6_rtmsg rtmsg;

	memset(&rtmsg, 0, sizeof(rtmsg));
	memcpy(&rtmsg.rtmsg_dst, pfx, sizeof(struct in6_addr));
	rtmsg.rtmsg_dst_len = plen;
	rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
	rtmsg.rtmsg_ifindex = dev->ifindex;
	rtmsg.rtmsg_info = expires;
	rtmsg.rtmsg_flags = RTF_UP|flags;
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;

	/* Prevent useless cloning on PtP SIT.
	   This thing is done here expecting that the whole
	   class of non-broadcast devices need not cloning.
	 */
	if (dev->type == ARPHRD_SIT && (dev->flags&IFF_POINTOPOINT))
		rtmsg.rtmsg_flags |= RTF_NONEXTHOP;

	ip6_route_add(&rtmsg);
}

/* Create "default" multicast route to the interface */

static void addrconf_add_mroute(struct device *dev)
{
	struct in6_rtmsg rtmsg;

	memset(&rtmsg, 0, sizeof(rtmsg));
	ipv6_addr_set(&rtmsg.rtmsg_dst,
		      __constant_htonl(0xFF000000), 0, 0, 0);
	rtmsg.rtmsg_dst_len = 8;
	rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
	rtmsg.rtmsg_ifindex = dev->ifindex;
	rtmsg.rtmsg_flags = RTF_UP|RTF_ADDRCONF;
	rtmsg.rtmsg_type = RTMSG_NEWROUTE;
	ip6_route_add(&rtmsg);
}

static void sit_route_add(struct device *dev)
{
	struct in6_rtmsg rtmsg;

	memset(&rtmsg, 0, sizeof(rtmsg));

	rtmsg.rtmsg_type	= RTMSG_NEWROUTE;
	rtmsg.rtmsg_metric	= IP6_RT_PRIO_ADDRCONF;

	/* prefix length - 96 bytes "::d.d.d.d" */
	rtmsg.rtmsg_dst_len	= 96;
	rtmsg.rtmsg_flags	= RTF_UP|RTF_NONEXTHOP;
	rtmsg.rtmsg_ifindex	= dev->ifindex;

	ip6_route_add(&rtmsg);
}

static void addrconf_add_lroute(struct device *dev)
{
	struct in6_addr addr;

	ipv6_addr_set(&addr,  __constant_htonl(0xFE800000), 0, 0, 0);
	addrconf_prefix_route(&addr, 10, dev, 0, RTF_ADDRCONF);
}

static struct inet6_dev *addrconf_add_dev(struct device *dev)
{
	struct inet6_dev *idev;

	if ((idev = ipv6_find_idev(dev)) == NULL)
		return NULL;

	/* Add default multicast route */
	addrconf_add_mroute(dev);

	/* Add link local route */
	addrconf_add_lroute(dev);
	return idev;
}

void addrconf_prefix_rcv(struct device *dev, u8 *opt, int len)
{
	struct prefix_info *pinfo;
	struct rt6_info *rt;
	__u32 valid_lft;
	__u32 prefered_lft;
	int addr_type;
	unsigned long rt_expires;
	struct inet6_dev *in6_dev = ipv6_get_idev(dev);

	if (in6_dev == NULL) {
		printk(KERN_DEBUG "addrconf: device %s not configured\n", dev->name);
		return;
	}

	pinfo = (struct prefix_info *) opt;
	
	if (len < sizeof(struct prefix_info)) {
		ADBG(("addrconf: prefix option too short\n"));
		return;
	}
	
	/*
	 *	Validation checks ([ADDRCONF], page 19)
	 */

	addr_type = ipv6_addr_type(&pinfo->prefix);

	if (addr_type & (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL))
		return;

	valid_lft = ntohl(pinfo->valid);
	prefered_lft = ntohl(pinfo->prefered);

	if (prefered_lft > valid_lft) {
		printk(KERN_WARNING "addrconf: prefix option has invalid lifetime\n");
		return;
	}

	/*
	 *	Two things going on here:
	 *	1) Add routes for on-link prefixes
	 *	2) Configure prefixes with the auto flag set
	 */

	/* Avoid arithemtic overflow. Really, we could
	   save rt_expires in seconds, likely valid_lft,
	   but it would require division in fib gc, that it
	   not good.
	 */
	if (valid_lft >= 0x7FFFFFFF/HZ)
		rt_expires = 0;
	else
		rt_expires = jiffies + valid_lft * HZ;

	rt = rt6_lookup(&pinfo->prefix, NULL, dev->ifindex, 1);

	if (rt && ((rt->rt6i_flags & (RTF_GATEWAY | RTF_DEFAULT)) == 0)) {
		if (rt->rt6i_flags&RTF_EXPIRES) {
			if (pinfo->onlink == 0 || valid_lft == 0) {
				ip6_del_rt(rt);
			} else {
				rt->rt6i_expires = rt_expires;
			}
		}
	} else if (pinfo->onlink && valid_lft) {
		addrconf_prefix_route(&pinfo->prefix, pinfo->prefix_len,
				      dev, rt_expires, RTF_ADDRCONF|RTF_EXPIRES);
	}
	if (rt)
		dst_release(&rt->u.dst);

	/* Try to figure out our local address for this prefix */

	if (pinfo->autoconf && in6_dev->cnf.autoconf) {
		struct inet6_ifaddr * ifp;
		struct in6_addr addr;
		int plen;

		plen = pinfo->prefix_len >> 3;

#ifdef CONFIG_IPV6_EUI64
		if (pinfo->prefix_len == 64) {
			memcpy(&addr, &pinfo->prefix, 8);
			if (ipv6_generate_eui64(addr.s6_addr + 8, dev) &&
			    ipv6_inherit_eui64(addr.s6_addr + 8, in6_dev))
				return;
			goto ok;
		}
#endif
#ifndef CONFIG_IPV6_NO_PB
		if (pinfo->prefix_len == ((sizeof(struct in6_addr) - dev->addr_len)<<3)) {
			memcpy(&addr, &pinfo->prefix, plen);
			memcpy(addr.s6_addr + plen, dev->dev_addr,
			       dev->addr_len);
			goto ok;
		}
#endif
		printk(KERN_DEBUG "IPv6 addrconf: prefix with wrong length %d\n", pinfo->prefix_len);
		return;

ok:
		ifp = ipv6_chk_addr(&addr, dev, 1);

		if ((ifp == NULL || (ifp->flags&ADDR_INVALID)) && valid_lft) {

			if (ifp == NULL)
				ifp = ipv6_add_addr(in6_dev, &addr, addr_type & IPV6_ADDR_SCOPE_MASK);

			if (ifp == NULL)
				return;

			ifp->prefix_len = pinfo->prefix_len;

			addrconf_dad_start(ifp);
		}

		if (ifp && valid_lft == 0) {
			ipv6_del_addr(ifp);
			ifp = NULL;
		}

		if (ifp) {
			int event = 0;
			ifp->valid_lft = valid_lft;
			ifp->prefered_lft = prefered_lft;
			ifp->tstamp = jiffies;
			if (ifp->flags & ADDR_INVALID)
				event = RTM_NEWADDR;
			ifp->flags &= ~(ADDR_DEPRECATED|ADDR_INVALID);
			ipv6_ifa_notify(event, ifp);
		}
	}
}

#ifndef _HURD_
/*
 *	Set destination address.
 *	Special case for SIT interfaces where we create a new "virtual"
 *	device.
 */
int addrconf_set_dstaddr(void *arg)
{
	struct in6_ifreq ireq;
	struct device *dev;
	int err = -EINVAL;

	rtnl_lock();

	err = -EFAULT;
	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		goto err_exit;

	dev = dev_get_by_index(ireq.ifr6_ifindex);

	err = -ENODEV;
	if (dev == NULL)
		goto err_exit;

	if (dev->type == ARPHRD_SIT) {
		struct ifreq ifr;
		mm_segment_t	oldfs;
		struct ip_tunnel_parm p;

		err = -EADDRNOTAVAIL;
		if (!(ipv6_addr_type(&ireq.ifr6_addr) & IPV6_ADDR_COMPATv4))
			goto err_exit;

		memset(&p, 0, sizeof(p));
		p.iph.daddr = ireq.ifr6_addr.s6_addr32[3];
		p.iph.saddr = 0;
		p.iph.version = 4;
		p.iph.ihl = 5;
		p.iph.protocol = IPPROTO_IPV6;
		p.iph.ttl = 64;
		ifr.ifr_ifru.ifru_data = (void*)&p;

		oldfs = get_fs(); set_fs(KERNEL_DS);
		err = dev->do_ioctl(dev, &ifr, SIOCADDTUNNEL);
		set_fs(oldfs);

		if (err == 0) {
			err = -ENOBUFS;
			if ((dev = dev_get(p.name)) == NULL)
				goto err_exit;
			err = dev_open(dev);
		}
	}

err_exit:
	rtnl_unlock();
	return err;
}
#endif /* not _HURD_ */

/*
 *	Manual configuration of address on an interface
 */
#ifndef _HURD_
static 
#endif
int inet6_addr_add(int ifindex, struct in6_addr *pfx, int plen)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct device *dev;
	int scope;
	
	if ((dev = dev_get_by_index(ifindex)) == NULL)
		return -ENODEV;
	
	if (!(dev->flags&IFF_UP))
		return -ENETDOWN;

	if ((idev = addrconf_add_dev(dev)) == NULL)
		return -ENOBUFS;

	scope = ipv6_addr_scope(pfx);

	addrconf_lock();
	if ((ifp = ipv6_add_addr(idev, pfx, scope)) != NULL) {
		ifp->prefix_len = plen;
		ifp->flags |= ADDR_PERMANENT;
		addrconf_dad_start(ifp);
		addrconf_unlock();
		return 0;
	}
	addrconf_unlock();

	return -ENOBUFS;
}

#ifndef _HURD_
static 
#endif
int inet6_addr_del(int ifindex, struct in6_addr *pfx, int plen)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct device *dev;
	
	if ((dev = dev_get_by_index(ifindex)) == NULL)
		return -ENODEV;

	if ((idev = ipv6_get_idev(dev)) == NULL)
		return -ENXIO;

	start_bh_atomic();
	for (ifp = idev->addr_list; ifp; ifp=ifp->if_next) {
		if (ifp->prefix_len == plen &&
		    (!memcmp(pfx, &ifp->addr, sizeof(struct in6_addr)))) {
			ipv6_del_addr(ifp);
			end_bh_atomic();

			/* If the last address is deleted administratively,
			   disable IPv6 on this interface.
			 */
			if (idev->addr_list == NULL)
				addrconf_ifdown(idev->dev, 1);
			return 0;
		}
	}
	end_bh_atomic();
	return -EADDRNOTAVAIL;
}


#ifndef _HURD_
int addrconf_add_ifaddr(void *arg)
{
	struct in6_ifreq ireq;
	int err;
	
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	
	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_add(ireq.ifr6_ifindex, &ireq.ifr6_addr, ireq.ifr6_prefixlen);
	rtnl_unlock();
	return err;
}
#endif /* not _HURD_ */

#ifndef _HURD_
int addrconf_del_ifaddr(void *arg)
{
	struct in6_ifreq ireq;
	int err;
	
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_del(ireq.ifr6_ifindex, &ireq.ifr6_addr, ireq.ifr6_prefixlen);
	rtnl_unlock();
	return err;
}
#endif /* not _HURD_ */

static void sit_add_v4_addrs(struct inet6_dev *idev)
{
	struct inet6_ifaddr * ifp;
	struct in6_addr addr;
	struct device *dev;
	int scope;

	memset(&addr, 0, sizeof(struct in6_addr));
	memcpy(&addr.s6_addr32[3], idev->dev->dev_addr, 4);

	if (idev->dev->flags&IFF_POINTOPOINT) {
		addr.s6_addr32[0] = __constant_htonl(0xfe800000);
		scope = IFA_LINK;
	} else {
		scope = IPV6_ADDR_COMPATv4;
	}

	if (addr.s6_addr32[3]) {
		addrconf_lock();
		ifp = ipv6_add_addr(idev, &addr, scope);
		if (ifp) {
			ifp->flags |= ADDR_PERMANENT;
			ifp->prefix_len = 128;
			ipv6_ifa_notify(RTM_NEWADDR, ifp);
		}
		addrconf_unlock();
		return;
	}

        for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->ip_ptr && (dev->flags & IFF_UP)) {
			struct in_device * in_dev = dev->ip_ptr;
			struct in_ifaddr * ifa;

			int flag = scope;

			for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
				addr.s6_addr32[3] = ifa->ifa_local;
				
				if (ifa->ifa_scope == RT_SCOPE_LINK)
					continue;
				if (ifa->ifa_scope >= RT_SCOPE_HOST) {
					if (idev->dev->flags&IFF_POINTOPOINT)
						continue;
					flag |= IFA_HOST;
				}

				addrconf_lock();
				ifp = ipv6_add_addr(idev, &addr, flag);
				if (ifp) {
					if (idev->dev->flags&IFF_POINTOPOINT)
						ifp->prefix_len = 10;
					else
						ifp->prefix_len = 96;
					ifp->flags |= ADDR_PERMANENT;
					ipv6_ifa_notify(RTM_NEWADDR, ifp);
				}
				addrconf_unlock();
			}
		}
        }
}

static void init_loopback(struct device *dev)
{
	struct in6_addr addr;
	struct inet6_dev  *idev;
	struct inet6_ifaddr * ifp;

	/* ::1 */

	memset(&addr, 0, sizeof(struct in6_addr));
	addr.s6_addr[15] = 1;

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init loopback: add_dev failed\n");
		return;
	}

	addrconf_lock();
	ifp = ipv6_add_addr(idev, &addr, IFA_HOST);

	if (ifp) {
		ifp->flags |= ADDR_PERMANENT;
		ifp->prefix_len = 128;
		ipv6_ifa_notify(RTM_NEWADDR, ifp);
	}
	addrconf_unlock();
}

static void addrconf_add_linklocal(struct inet6_dev *idev, struct in6_addr *addr)
{
	struct inet6_ifaddr * ifp;

	addrconf_lock();
	ifp = ipv6_add_addr(idev, addr, IFA_LINK);
	if (ifp) {
		ifp->flags = ADDR_PERMANENT;
		ifp->prefix_len = 10;
		addrconf_dad_start(ifp);
	}
	addrconf_unlock();
}

static void addrconf_dev_config(struct device *dev)
{
	struct in6_addr addr;
	struct inet6_dev    * idev;

	if (dev->type != ARPHRD_ETHER) {
		/* Alas, we support only Ethernet autoconfiguration. */
		return;
	}

	idev = addrconf_add_dev(dev);
	if (idev == NULL)
		return;

#ifdef CONFIG_IPV6_EUI64
	memset(&addr, 0, sizeof(struct in6_addr));

	addr.s6_addr[0] = 0xFE;
	addr.s6_addr[1] = 0x80;

	if (ipv6_generate_eui64(addr.s6_addr + 8, dev) == 0)
		addrconf_add_linklocal(idev, &addr);
#endif

#ifndef CONFIG_IPV6_NO_PB
	memset(&addr, 0, sizeof(struct in6_addr));

	addr.s6_addr[0] = 0xFE;
	addr.s6_addr[1] = 0x80;

	memcpy(addr.s6_addr + (sizeof(struct in6_addr) - dev->addr_len), 
	       dev->dev_addr, dev->addr_len);
	addrconf_add_linklocal(idev, &addr);
#endif
}

static void addrconf_sit_config(struct device *dev)
{
	struct inet6_dev *idev;

	/* 
	 * Configure the tunnel with one of our IPv4 
	 * addresses... we should configure all of 
	 * our v4 addrs in the tunnel
	 */

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init sit: add_dev failed\n");
		return;
	}

	sit_add_v4_addrs(idev);

	if (dev->flags&IFF_POINTOPOINT) {
		addrconf_add_mroute(dev);
		addrconf_add_lroute(dev);
	} else
		sit_route_add(dev);
}


int addrconf_notify(struct notifier_block *this, unsigned long event, 
		    void * data)
{
	struct device *dev;

	dev = (struct device *) data;

	switch(event) {
	case NETDEV_UP:
		switch(dev->type) {
		case ARPHRD_SIT:
			addrconf_sit_config(dev);
			break;

		case ARPHRD_LOOPBACK:
			init_loopback(dev);
			break;

		default:
			addrconf_dev_config(dev);
			break;
		};

#ifdef CONFIG_IPV6_NETLINK
		rt6_sndmsg(RTMSG_NEWDEVICE, NULL, NULL, NULL, dev, 0, 0, 0, 0);
#endif
		break;

	case NETDEV_CHANGEMTU:
		if (dev->mtu >= IPV6_MIN_MTU) {
			struct inet6_dev *idev;

			if ((idev = ipv6_get_idev(dev)) == NULL)
				break;
			idev->cnf.mtu6 = dev->mtu;
			rt6_mtu_change(dev, dev->mtu);
			break;
		}

		/* MTU falled under IPV6_MIN_MTU. Stop IPv6 on this interface. */

	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
		/*
		 *	Remove all addresses from this interface.
		 */
		if (addrconf_ifdown(dev, event != NETDEV_DOWN) == 0) {
#ifdef CONFIG_IPV6_NETLINK
			rt6_sndmsg(RTMSG_DELDEVICE, NULL, NULL, NULL, dev, 0, 0, 0, 0);
#endif
		}

		break;
	case NETDEV_CHANGE:
		break;
	};

	return NOTIFY_OK;
}

static int addrconf_ifdown(struct device *dev, int how)
{
	struct inet6_dev *idev, **bidev;
	struct inet6_ifaddr *ifa, **bifa;
	int i, hash;

	rt6_ifdown(dev);
	neigh_ifdown(&nd_tbl, dev);

	idev = ipv6_get_idev(dev);
	if (idev == NULL)
		return -ENODEV;

	start_bh_atomic();

	/* Discard address list */

	idev->addr_list = NULL;

	/*
	 * Clean addresses hash table
	 */

	for (i=0; i<16; i++) {
		bifa = &inet6_addr_lst[i];

		while ((ifa = *bifa) != NULL) {
			if (ifa->idev == idev) {
				*bifa = ifa->lst_next;
				del_timer(&ifa->timer);
				ipv6_ifa_notify(RTM_DELADDR, ifa);
				kfree(ifa);
				continue;
			}
			bifa = &ifa->lst_next;
		}
	}

	/* Discard multicast list */

	if (how == 1)
		ipv6_mc_destroy_dev(idev);
	else
		ipv6_mc_down(idev);

	/* Delete device from device hash table (if unregistered) */

	if (how == 1) {
		hash = ipv6_devindex_hash(dev->ifindex);

		for (bidev = &inet6_dev_lst[hash]; (idev=*bidev) != NULL; bidev = &idev->next) {
			if (idev->dev == dev) {
				*bidev = idev->next;
				neigh_parms_release(&nd_tbl, idev->nd_parms);
#ifdef CONFIG_SYSCTL
				addrconf_sysctl_unregister(&idev->cnf);
#endif
				kfree(idev);
				break;
			}
		}
	}
	end_bh_atomic();
	return 0;
}


static void addrconf_rs_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;

	ifp = (struct inet6_ifaddr *) data;

	if (ifp->idev->cnf.forwarding)
		return;

	if (ifp->idev->if_flags & IF_RA_RCVD) {
		/*
		 *	Announcement received after solicitation
		 *	was sent
		 */
		return;
	}

	if (ifp->probes++ <= ifp->idev->cnf.rtr_solicits) {
		struct in6_addr all_routers;

		ipv6_addr_all_routers(&all_routers);

		ndisc_send_rs(ifp->idev->dev, &ifp->addr, &all_routers);
		
		ifp->timer.function = addrconf_rs_timer;
		ifp->timer.expires = (jiffies + 
				      ifp->idev->cnf.rtr_solicit_interval);
		add_timer(&ifp->timer);
	} else {
		struct in6_rtmsg rtmsg;

		printk(KERN_DEBUG "%s: no IPv6 routers present\n",
		       ifp->idev->dev->name);

		memset(&rtmsg, 0, sizeof(struct in6_rtmsg));
		rtmsg.rtmsg_type = RTMSG_NEWROUTE;
		rtmsg.rtmsg_metric = IP6_RT_PRIO_ADDRCONF;
		rtmsg.rtmsg_flags = (RTF_ALLONLINK | RTF_ADDRCONF | 
				     RTF_DEFAULT | RTF_UP);

		rtmsg.rtmsg_ifindex = ifp->idev->dev->ifindex;

		ip6_route_add(&rtmsg);
	}
}

/*
 *	Duplicate Address Detection
 */
static void addrconf_dad_start(struct inet6_ifaddr *ifp)
{
	struct device *dev;
	unsigned long rand_num;

	dev = ifp->idev->dev;

	addrconf_join_solict(dev, &ifp->addr);

	if (ifp->prefix_len != 128 && (ifp->flags&ADDR_PERMANENT))
		addrconf_prefix_route(&ifp->addr, ifp->prefix_len, dev, 0, RTF_ADDRCONF);

	if (dev->flags&(IFF_NOARP|IFF_LOOPBACK)) {
		start_bh_atomic();
		ifp->flags &= ~DAD_INCOMPLETE;
		addrconf_dad_completed(ifp);
		end_bh_atomic();
		return;
	}

	net_srandom(ifp->addr.s6_addr32[3]);

	ifp->probes = ifp->idev->cnf.dad_transmits;
	ifp->flags |= DAD_INCOMPLETE;

	rand_num = net_random() % ifp->idev->cnf.rtr_solicit_delay;

	ifp->timer.function = addrconf_dad_timer;
	ifp->timer.expires = jiffies + rand_num;

	add_timer(&ifp->timer);
}

static void addrconf_dad_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp;
	struct in6_addr unspec;
	struct in6_addr mcaddr;

	ifp = (struct inet6_ifaddr *) data;

	if (ifp->probes == 0) {
		/*
		 * DAD was successful
		 */

		ifp->flags &= ~DAD_INCOMPLETE;
		addrconf_dad_completed(ifp);
		return;
	}

	ifp->probes--;

	/* send a neighbour solicitation for our addr */
	memset(&unspec, 0, sizeof(unspec));
#ifdef CONFIG_IPV6_EUI64
	addrconf_addr_solict_mult_new(&ifp->addr, &mcaddr);
	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &unspec);
#endif
#ifndef CONFIG_IPV6_NO_PB
	addrconf_addr_solict_mult_old(&ifp->addr, &mcaddr);
	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &unspec);
#endif

	ifp->timer.expires = jiffies + ifp->idev->cnf.rtr_solicit_interval;
	add_timer(&ifp->timer);
}

static void addrconf_dad_completed(struct inet6_ifaddr *ifp)
{
	struct device *	dev = ifp->idev->dev;

	/*
	 *	Configure the address for reception. Now it is valid.
	 */

	ipv6_ifa_notify(RTM_NEWADDR, ifp);

	/* If added prefix is link local and forwarding is off,
	   start sending router solicitations.
	 */

	if (ifp->idev->cnf.forwarding == 0 &&
	    (dev->flags&IFF_LOOPBACK) == 0 &&
	    (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL)) {
		struct in6_addr all_routers;

		ipv6_addr_all_routers(&all_routers);

		/*
		 *	If a host as already performed a random delay
		 *	[...] as part of DAD [...] there is no need
		 *	to delay again before sending the first RS
		 */
		ndisc_send_rs(ifp->idev->dev, &ifp->addr, &all_routers);

		ifp->probes = 1;
		ifp->timer.function = addrconf_rs_timer;
		ifp->timer.expires = (jiffies +
				      ifp->idev->cnf.rtr_solicit_interval);
		ifp->idev->if_flags |= IF_RS_SENT;
		add_timer(&ifp->timer);
	}
}

#ifdef CONFIG_PROC_FS
static int iface_proc_info(char *buffer, char **start, off_t offset,
			   int length, int dummy)
{
	struct inet6_ifaddr *ifp;
	int i;
	int len = 0;
	off_t pos=0;
	off_t begin=0;

	addrconf_lock();

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next) {
			int j;

			for (j=0; j<16; j++) {
				sprintf(buffer + len, "%02x",
					ifp->addr.s6_addr[j]);
				len += 2;
			}

			len += sprintf(buffer + len,
				       " %02x %02x %02x %02x %8s\n",
				       ifp->idev->dev->ifindex,
				       ifp->prefix_len,
				       ifp->scope,
				       ifp->flags,
				       ifp->idev->dev->name);
			pos=begin+len;
			if(pos<offset) {
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				goto done;
		}
	}

done:
	addrconf_unlock();

	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	if(len<0)
		len=0;
	return len;
}

struct proc_dir_entry iface_proc_entry =
{
        0, 8, "if_inet6",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL,
        &iface_proc_info
};
#endif	/* CONFIG_PROC_FS */

/*
 *	Periodic address status verification
 */

void addrconf_verify(unsigned long foo)
{
	struct inet6_ifaddr *ifp;
	unsigned long now = jiffies;
	int i;

	if (atomic_read(&addr_list_lock)) {
		addr_chk_timer.expires = jiffies + 1*HZ;
		add_timer(&addr_chk_timer);
		return;
	}

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifp=inet6_addr_lst[i]; ifp;) {
			if (ifp->flags & ADDR_INVALID) {
				struct inet6_ifaddr *bp = ifp;
				ifp= ifp->lst_next;
				ipv6_del_addr(bp);
				continue;
			}
			if (!(ifp->flags & ADDR_PERMANENT)) {
				struct inet6_ifaddr *bp;
				unsigned long age;

				age = (now - ifp->tstamp) / HZ;

				bp = ifp;
				ifp= ifp->lst_next;
				
				if (age > bp->valid_lft)
					ipv6_del_addr(bp);
				else if (age > bp->prefered_lft) {
					bp->flags |= ADDR_DEPRECATED;
					ipv6_ifa_notify(0, bp);
				}

				continue;
			}
			ifp = ifp->lst_next;
		}
	}

	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);
}

#ifdef CONFIG_RTNETLINK

static int
inet6_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in6_addr *pfx;

	pfx = NULL;
	if (rta[IFA_ADDRESS-1]) {
		if (RTA_PAYLOAD(rta[IFA_ADDRESS-1]) < sizeof(*pfx))
			return -EINVAL;
		pfx = RTA_DATA(rta[IFA_ADDRESS-1]);
	}
	if (rta[IFA_LOCAL-1]) {
		if (pfx && memcmp(pfx, RTA_DATA(rta[IFA_LOCAL-1]), sizeof(*pfx)))
			return -EINVAL;
		pfx = RTA_DATA(rta[IFA_LOCAL-1]);
	}
	if (pfx == NULL)
		return -EINVAL;

	return inet6_addr_del(ifm->ifa_index, pfx, ifm->ifa_prefixlen);
}

static int
inet6_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr  **rta = arg;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in6_addr *pfx;

	pfx = NULL;
	if (rta[IFA_ADDRESS-1]) {
		if (RTA_PAYLOAD(rta[IFA_ADDRESS-1]) < sizeof(*pfx))
			return -EINVAL;
		pfx = RTA_DATA(rta[IFA_ADDRESS-1]);
	}
	if (rta[IFA_LOCAL-1]) {
		if (pfx && memcmp(pfx, RTA_DATA(rta[IFA_LOCAL-1]), sizeof(*pfx)))
			return -EINVAL;
		pfx = RTA_DATA(rta[IFA_LOCAL-1]);
	}
	if (pfx == NULL)
		return -EINVAL;

	return inet6_addr_add(ifm->ifa_index, pfx, ifm->ifa_prefixlen);
}

static int inet6_fill_ifaddr(struct sk_buff *skb, struct inet6_ifaddr *ifa,
			     u32 pid, u32 seq, int event)
{
	struct ifaddrmsg *ifm;
	struct nlmsghdr  *nlh;
	struct ifa_cacheinfo ci;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*ifm));
	ifm = NLMSG_DATA(nlh);
	ifm->ifa_family = AF_INET6;
	ifm->ifa_prefixlen = ifa->prefix_len;
	ifm->ifa_flags = ifa->flags & ~ADDR_INVALID;
	ifm->ifa_scope = RT_SCOPE_UNIVERSE;
	if (ifa->scope&IFA_HOST)
		ifm->ifa_scope = RT_SCOPE_HOST;
	else if (ifa->scope&IFA_LINK)
		ifm->ifa_scope = RT_SCOPE_LINK;
	else if (ifa->scope&IFA_SITE)
		ifm->ifa_scope = RT_SCOPE_SITE;
	ifm->ifa_index = ifa->idev->dev->ifindex;
	RTA_PUT(skb, IFA_ADDRESS, 16, &ifa->addr);
	if (!(ifa->flags&IFA_F_PERMANENT)) {
		ci.ifa_prefered = ifa->prefered_lft;
		ci.ifa_valid = ifa->valid_lft;
		if (ci.ifa_prefered != 0xFFFFFFFF) {
			long tval = (jiffies - ifa->tstamp)/HZ;
			ci.ifa_prefered -= tval;
			if (ci.ifa_valid != 0xFFFFFFFF)
				ci.ifa_valid -= tval;
		}
		RTA_PUT(skb, IFA_CACHEINFO, sizeof(ci), &ci);
	}
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int inet6_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, ip_idx;
	int s_idx, s_ip_idx;
 	struct inet6_ifaddr *ifa;

	s_idx = cb->args[0];
	s_ip_idx = ip_idx = cb->args[1];

	for (idx=0; idx < IN6_ADDR_HSIZE; idx++) {
		if (idx < s_idx)
			continue;
		if (idx > s_idx)
			s_ip_idx = 0;
		start_bh_atomic();
		for (ifa=inet6_addr_lst[idx], ip_idx = 0; ifa;
		     ifa = ifa->lst_next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			if (inet6_fill_ifaddr(skb, ifa, NETLINK_CB(cb->skb).pid,
					      cb->nlh->nlmsg_seq, RTM_NEWADDR) <= 0) {
				end_bh_atomic();
				goto done;
			}
		}
		end_bh_atomic();
	}
done:
	cb->args[0] = idx;
	cb->args[1] = ip_idx;

	return skb->len;
}

static void inet6_ifa_notify(int event, struct inet6_ifaddr *ifa)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct ifaddrmsg)+128);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		netlink_set_err(rtnl, 0, RTMGRP_IPV6_IFADDR, ENOBUFS);
		return;
	}
	if (inet6_fill_ifaddr(skb, ifa, 0, 0, event) < 0) {
		kfree_skb(skb);
		netlink_set_err(rtnl, 0, RTMGRP_IPV6_IFADDR, EINVAL);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_IPV6_IFADDR;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_IPV6_IFADDR, GFP_ATOMIC);
}

static struct rtnetlink_link inet6_rtnetlink_table[RTM_MAX-RTM_BASE+1] =
{
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},

	{ inet6_rtm_newaddr,	NULL,			},
	{ inet6_rtm_deladdr,	NULL,			},
	{ NULL,			inet6_dump_ifaddr,	},
	{ NULL,			NULL,			},

	{ inet6_rtm_newroute,	NULL,			},
	{ inet6_rtm_delroute,	NULL,			},
	{ inet6_rtm_getroute,	inet6_dump_fib,		},
	{ NULL,			NULL,			},
};
#endif

static void ipv6_ifa_notify(int event, struct inet6_ifaddr *ifp)
{
#ifdef CONFIG_RTNETLINK
	inet6_ifa_notify(event ? : RTM_NEWADDR, ifp);
#endif
	switch (event) {
	case RTM_NEWADDR:
		ip6_rt_addr_add(&ifp->addr, ifp->idev->dev);
		break;
	case RTM_DELADDR:
		start_bh_atomic();
		addrconf_leave_solict(ifp->idev->dev, &ifp->addr);
		if (ipv6_chk_addr(&ifp->addr, ifp->idev->dev, 0) == NULL)
			ip6_rt_addr_del(&ifp->addr, ifp->idev->dev);
		end_bh_atomic();
		break;
	}
}

#ifdef CONFIG_SYSCTL

static
int addrconf_sysctl_forward(ctl_table *ctl, int write, struct file * filp,
			   void *buffer, size_t *lenp)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write && *valp != val && valp != &ipv6_devconf_dflt.forwarding) {
		struct inet6_dev *idev = NULL;

		if (valp != &ipv6_devconf.forwarding) {
			struct device *dev = dev_get_by_index(ctl->ctl_name);
			if (dev)
				idev = ipv6_get_idev(dev);
			if (idev == NULL)
				return ret;
		} else
			ipv6_devconf_dflt.forwarding = ipv6_devconf.forwarding;

		addrconf_forward_change(idev);

		if (*valp) {
			start_bh_atomic();
			rt6_purge_dflt_routers(0);
			end_bh_atomic();
		}
	}

        return ret;
}

static struct addrconf_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table addrconf_vars[11];
	ctl_table addrconf_dev[2];
	ctl_table addrconf_conf_dir[2];
	ctl_table addrconf_proto_dir[2];
	ctl_table addrconf_root_dir[2];
} addrconf_sysctl = {
	NULL,
        {{NET_IPV6_FORWARDING, "forwarding",
         &ipv6_devconf.forwarding, sizeof(int), 0644, NULL,
         &addrconf_sysctl_forward},

	{NET_IPV6_HOP_LIMIT, "hop_limit",
         &ipv6_devconf.hop_limit, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_MTU, "mtu",
         &ipv6_devconf.mtu6, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ACCEPT_RA, "accept_ra",
         &ipv6_devconf.accept_ra, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_ACCEPT_REDIRECTS, "accept_redirects",
         &ipv6_devconf.accept_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_AUTOCONF, "autoconf",
         &ipv6_devconf.autoconf, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_DAD_TRANSMITS, "dad_transmits",
         &ipv6_devconf.dad_transmits, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_RTR_SOLICITS, "router_solicitations",
         &ipv6_devconf.rtr_solicits, sizeof(int), 0644, NULL,
         &proc_dointvec},

	{NET_IPV6_RTR_SOLICIT_INTERVAL, "router_solicitation_interval",
         &ipv6_devconf.rtr_solicit_interval, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},

	{NET_IPV6_RTR_SOLICIT_DELAY, "router_solicitation_delay",
         &ipv6_devconf.rtr_solicit_delay, sizeof(int), 0644, NULL,
         &proc_dointvec_jiffies},

	{0}},

	{{NET_PROTO_CONF_ALL, "all", NULL, 0, 0555, addrconf_sysctl.addrconf_vars},{0}},
	{{NET_IPV6_CONF, "conf", NULL, 0, 0555, addrconf_sysctl.addrconf_dev},{0}},
	{{NET_IPV6, "ipv6", NULL, 0, 0555, addrconf_sysctl.addrconf_conf_dir},{0}},
	{{CTL_NET, "net", NULL, 0, 0555, addrconf_sysctl.addrconf_proto_dir},{0}}
};

static void addrconf_sysctl_register(struct inet6_dev *idev, struct ipv6_devconf *p)
{
	int i;
	struct device *dev = idev ? idev->dev : NULL;
	struct addrconf_sysctl_table *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;
	memcpy(t, &addrconf_sysctl, sizeof(*t));
	for (i=0; i<sizeof(t->addrconf_vars)/sizeof(t->addrconf_vars[0])-1; i++) {
		t->addrconf_vars[i].data += (char*)p - (char*)&ipv6_devconf;
		t->addrconf_vars[i].de = NULL;
	}
	if (dev) {
		t->addrconf_dev[0].procname = dev->name;
		t->addrconf_dev[0].ctl_name = dev->ifindex;
	} else {
		t->addrconf_dev[0].procname = "default";
		t->addrconf_dev[0].ctl_name = NET_PROTO_CONF_DEFAULT;
	}
	t->addrconf_dev[0].child = t->addrconf_vars;
	t->addrconf_dev[0].de = NULL;
	t->addrconf_conf_dir[0].child = t->addrconf_dev;
	t->addrconf_conf_dir[0].de = NULL;
	t->addrconf_proto_dir[0].child = t->addrconf_conf_dir;
	t->addrconf_proto_dir[0].de = NULL;
	t->addrconf_root_dir[0].child = t->addrconf_proto_dir;
	t->addrconf_root_dir[0].de = NULL;

	t->sysctl_header = register_sysctl_table(t->addrconf_root_dir, 0);
	if (t->sysctl_header == NULL)
		kfree(t);
	else
		p->sysctl = t;
}

static void addrconf_sysctl_unregister(struct ipv6_devconf *p)
{
	if (p->sysctl) {
		struct addrconf_sysctl_table *t = p->sysctl;
		p->sysctl = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t);
	}
}


#endif

/*
 *	Init / cleanup code
 */

__initfunc(void addrconf_init(void))
{
#ifdef MODULE
	struct device *dev;

	/* This takes sense only during module load. */

	for (dev = dev_base; dev; dev = dev->next) {
		if (!(dev->flags&IFF_UP))
			continue;

		switch (dev->type) {
		case ARPHRD_LOOPBACK:	
			init_loopback(dev);
			break;
		case ARPHRD_ETHER:	
			addrconf_dev_config(dev);
			break;
		default:
			/* Ignore all other */
		}
	}
#endif
	
#ifdef CONFIG_PROC_FS
	proc_net_register(&iface_proc_entry);
#endif
	
	addr_chk_timer.expires = jiffies + ADDR_CHECK_FREQUENCY;
	add_timer(&addr_chk_timer);
#ifdef CONFIG_RTNETLINK
	rtnetlink_links[PF_INET6] = inet6_rtnetlink_table;
#endif
#ifdef CONFIG_SYSCTL
	addrconf_sysctl.sysctl_header =
		register_sysctl_table(addrconf_sysctl.addrconf_root_dir, 0);
	addrconf_sysctl_register(NULL, &ipv6_devconf_dflt);
#endif
}

#ifdef MODULE
void addrconf_cleanup(void)
{
 	struct inet6_dev *idev;
 	struct inet6_ifaddr *ifa;
	int i;

#ifdef CONFIG_RTNETLINK
	rtnetlink_links[PF_INET6] = NULL;
#endif
#ifdef CONFIG_SYSCTL
	addrconf_sysctl_unregister(&ipv6_devconf_dflt);
	addrconf_sysctl_unregister(&ipv6_devconf);
#endif

	del_timer(&addr_chk_timer);

	/*
	 *	clean dev list.
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		struct inet6_dev *next;
		for (idev = inet6_dev_lst[i]; idev; idev = next) {
			next = idev->next;
			addrconf_ifdown(idev->dev, 1);
		}
	}

	start_bh_atomic();
	/*
	 *	clean addr_list
	 */

	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifa=inet6_addr_lst[i]; ifa; ) {
			struct inet6_ifaddr *bifa;

			bifa = ifa;
			ifa = ifa->lst_next;
			printk(KERN_DEBUG "bug: IPv6 address leakage detected: ifa=%p\n", bifa);
			/* Do not free it; something is wrong.
			   Now we can investigate it with debugger.
			 */
		}
	}
	end_bh_atomic();

#ifdef CONFIG_PROC_FS
	proc_net_unregister(iface_proc_entry.low_ino);
#endif
}
#endif	/* MODULE */
