/*
 *	NET3	IP device support routines.
 *
 *	Version: $Id: devinet.c,v 1.28.2.2 1999/08/07 10:56:18 davem Exp $
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	Changes:
 *	        Alexey Kuznetsov:	pa_* fields are replaced with ifaddr lists.
 *		Cyrus Durgin:		updated for kmod
 */

#include <linux/config.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include <net/ip.h>
#include <net/route.h>
#include <net/ip_fib.h>

struct ipv4_devconf ipv4_devconf = { 1, 1, 1, 1, 0, };
static struct ipv4_devconf ipv4_devconf_dflt = { 1, 1, 1, 1, 1, };

#ifdef CONFIG_RTNETLINK
static void rtmsg_ifa(int event, struct in_ifaddr *);
#else
#define rtmsg_ifa(a,b)	do { } while(0)
#endif

static struct notifier_block *inetaddr_chain;
static void inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap, int destroy);
#ifdef CONFIG_SYSCTL
static void devinet_sysctl_register(struct in_device *in_dev, struct ipv4_devconf *p);
static void devinet_sysctl_unregister(struct ipv4_devconf *p);
#endif

int inet_ifa_count;
int inet_dev_count;

static struct in_ifaddr * inet_alloc_ifa(void)
{
	struct in_ifaddr *ifa;

	ifa = kmalloc(sizeof(*ifa), GFP_KERNEL);
	if (ifa) {
		memset(ifa, 0, sizeof(*ifa));
		inet_ifa_count++;
	}

	return ifa;
}

static __inline__ void inet_free_ifa(struct in_ifaddr *ifa)
{
	kfree_s(ifa, sizeof(*ifa));
	inet_ifa_count--;
}

struct in_device *inetdev_init(struct device *dev)
{
	struct in_device *in_dev;

	if (dev->mtu < 68)
		return NULL;

	in_dev = kmalloc(sizeof(*in_dev), GFP_KERNEL);
	if (!in_dev)
		return NULL;
	inet_dev_count++;
	memset(in_dev, 0, sizeof(*in_dev));
	memcpy(&in_dev->cnf, &ipv4_devconf_dflt, sizeof(in_dev->cnf));
	in_dev->cnf.sysctl = NULL;
	in_dev->dev = dev;
	if ((in_dev->arp_parms = neigh_parms_alloc(dev, &arp_tbl)) == NULL) {
		kfree(in_dev);
		return NULL;
	}
#ifdef CONFIG_SYSCTL
	neigh_sysctl_register(dev, in_dev->arp_parms, NET_IPV4, NET_IPV4_NEIGH, "ipv4");
#endif
	dev->ip_ptr = in_dev;
#ifdef CONFIG_SYSCTL
	devinet_sysctl_register(in_dev, &in_dev->cnf);
#endif
	if (dev->flags&IFF_UP)
		ip_mc_up(in_dev);
	return in_dev;
}

static void inetdev_destroy(struct in_device *in_dev)
{
	struct in_ifaddr *ifa;

	ip_mc_destroy_dev(in_dev);

	while ((ifa = in_dev->ifa_list) != NULL) {
		inet_del_ifa(in_dev, &in_dev->ifa_list, 0);
		inet_free_ifa(ifa);
	}

#ifdef CONFIG_SYSCTL
	devinet_sysctl_unregister(&in_dev->cnf);
#endif
	in_dev->dev->ip_ptr = NULL;
	synchronize_bh();
	neigh_parms_release(&arp_tbl, in_dev->arp_parms);
	kfree(in_dev);
}

struct in_ifaddr * inet_addr_onlink(struct in_device *in_dev, u32 a, u32 b)
{
	for_primary_ifa(in_dev) {
		if (inet_ifa_match(a, ifa)) {
			if (!b || inet_ifa_match(b, ifa))
				return ifa;
		}
	} endfor_ifa(in_dev);
	return NULL;
}

static void
inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap, int destroy)
{
	struct in_ifaddr *ifa1 = *ifap;

	/* 1. Deleting primary ifaddr forces deletion all secondaries */

	if (!(ifa1->ifa_flags&IFA_F_SECONDARY)) {
		struct in_ifaddr *ifa;
		struct in_ifaddr **ifap1 = &ifa1->ifa_next;

		while ((ifa=*ifap1) != NULL) {
			if (!(ifa->ifa_flags&IFA_F_SECONDARY) ||
			    ifa1->ifa_mask != ifa->ifa_mask ||
			    !inet_ifa_match(ifa1->ifa_address, ifa)) {
				ifap1 = &ifa->ifa_next;
				continue;
			}
			*ifap1 = ifa->ifa_next;
			synchronize_bh();

			rtmsg_ifa(RTM_DELADDR, ifa);
			notifier_call_chain(&inetaddr_chain, NETDEV_DOWN, ifa);
			inet_free_ifa(ifa);
		}
	}

	/* 2. Unlink it */

	*ifap = ifa1->ifa_next;
	synchronize_bh();

	/* 3. Announce address deletion */

	/* Send message first, then call notifier.
	   At first sight, FIB update triggered by notifier
	   will refer to already deleted ifaddr, that could confuse
	   netlink listeners. It is not true: look, gated sees
	   that route deleted and if it still thinks that ifaddr
	   is valid, it will try to restore deleted routes... Grr.
	   So that, this order is correct.
	 */
	rtmsg_ifa(RTM_DELADDR, ifa1);
	notifier_call_chain(&inetaddr_chain, NETDEV_DOWN, ifa1);
	if (destroy) {
		inet_free_ifa(ifa1);
		if (in_dev->ifa_list == NULL)
			inetdev_destroy(in_dev);
	}
}

static int
inet_insert_ifa(struct in_device *in_dev, struct in_ifaddr *ifa)
{
	struct in_ifaddr *ifa1, **ifap, **last_primary;

#ifndef _HURD_
	if (ifa->ifa_local == 0) {
		inet_free_ifa(ifa);
		return 0;
	}
#endif

	ifa->ifa_flags &= ~IFA_F_SECONDARY;
	last_primary = &in_dev->ifa_list;

	for (ifap=&in_dev->ifa_list; (ifa1=*ifap)!=NULL; ifap=&ifa1->ifa_next) {
		if (!(ifa1->ifa_flags&IFA_F_SECONDARY) && ifa->ifa_scope <= ifa1->ifa_scope)
			last_primary = &ifa1->ifa_next;
		if (ifa1->ifa_mask == ifa->ifa_mask && inet_ifa_match(ifa1->ifa_address, ifa)) {
			if (ifa1->ifa_local == ifa->ifa_local) {
				inet_free_ifa(ifa);
				return -EEXIST;
			}
			if (ifa1->ifa_scope != ifa->ifa_scope) {
				inet_free_ifa(ifa);
				return -EINVAL;
			}
			ifa->ifa_flags |= IFA_F_SECONDARY;
		}
	}

	if (!(ifa->ifa_flags&IFA_F_SECONDARY)) {
		net_srandom(ifa->ifa_local);
		ifap = last_primary;
	}

	ifa->ifa_next = *ifap;
	wmb();
	*ifap = ifa;

	/* Send message first, then call notifier.
	   Notifier will trigger FIB update, so that
	   listeners of netlink will know about new ifaddr */
	rtmsg_ifa(RTM_NEWADDR, ifa);
	notifier_call_chain(&inetaddr_chain, NETDEV_UP, ifa);

	return 0;
}

static int
inet_set_ifa(struct device *dev, struct in_ifaddr *ifa)
{
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL) {
		in_dev = inetdev_init(dev);
		if (in_dev == NULL) {
			inet_free_ifa(ifa);
			return -ENOBUFS;
		}
	}
	ifa->ifa_dev = in_dev;
	if (LOOPBACK(ifa->ifa_local))
		ifa->ifa_scope = RT_SCOPE_HOST;
	return inet_insert_ifa(in_dev, ifa);
}

struct in_device *inetdev_by_index(int ifindex)
{
	struct device *dev;
	dev = dev_get_by_index(ifindex);
	if (dev)
		return dev->ip_ptr;
	return NULL;
}

struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, u32 prefix, u32 mask)
{
	for_primary_ifa(in_dev) {
		if (ifa->ifa_mask == mask && inet_ifa_match(prefix, ifa))
			return ifa;
	} endfor_ifa(in_dev);
	return NULL;
}

#ifdef CONFIG_RTNETLINK

/* rtm_{add|del} functions are not reenterable, so that
   this structure can be made static
 */

int
inet_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr  **rta = arg;
	struct in_device *in_dev;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in_ifaddr *ifa, **ifap;

	if ((in_dev = inetdev_by_index(ifm->ifa_index)) == NULL)
		return -EADDRNOTAVAIL;

	for (ifap=&in_dev->ifa_list; (ifa=*ifap)!=NULL; ifap=&ifa->ifa_next) {
		if ((rta[IFA_LOCAL-1] && memcmp(RTA_DATA(rta[IFA_LOCAL-1]), &ifa->ifa_local, 4)) ||
		    (rta[IFA_LABEL-1] && strcmp(RTA_DATA(rta[IFA_LABEL-1]), ifa->ifa_label)) ||
		    (rta[IFA_ADDRESS-1] &&
		     (ifm->ifa_prefixlen != ifa->ifa_prefixlen ||
		      !inet_ifa_match(*(u32*)RTA_DATA(rta[IFA_ADDRESS-1]), ifa))))
			continue;
		inet_del_ifa(in_dev, ifap, 1);
		return 0;
	}

	return -EADDRNOTAVAIL;
}

int
inet_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct device *dev;
	struct in_device *in_dev;
	struct ifaddrmsg *ifm = NLMSG_DATA(nlh);
	struct in_ifaddr *ifa;

	if (ifm->ifa_prefixlen > 32 || rta[IFA_LOCAL-1] == NULL)
		return -EINVAL;

	if ((dev = dev_get_by_index(ifm->ifa_index)) == NULL)
		return -ENODEV;

	if ((in_dev = dev->ip_ptr) == NULL) {
		in_dev = inetdev_init(dev);
		if (!in_dev)
			return -ENOBUFS;
	}

	if ((ifa = inet_alloc_ifa()) == NULL)
		return -ENOBUFS;

	if (rta[IFA_ADDRESS-1] == NULL)
		rta[IFA_ADDRESS-1] = rta[IFA_LOCAL-1];
	memcpy(&ifa->ifa_local, RTA_DATA(rta[IFA_LOCAL-1]), 4);
	memcpy(&ifa->ifa_address, RTA_DATA(rta[IFA_ADDRESS-1]), 4);
	ifa->ifa_prefixlen = ifm->ifa_prefixlen;
	ifa->ifa_mask = inet_make_mask(ifm->ifa_prefixlen);
	if (rta[IFA_BROADCAST-1])
		memcpy(&ifa->ifa_broadcast, RTA_DATA(rta[IFA_BROADCAST-1]), 4);
	if (rta[IFA_ANYCAST-1])
		memcpy(&ifa->ifa_anycast, RTA_DATA(rta[IFA_ANYCAST-1]), 4);
	ifa->ifa_flags = ifm->ifa_flags;
	ifa->ifa_scope = ifm->ifa_scope;
	ifa->ifa_dev = in_dev;
	if (rta[IFA_LABEL-1])
		memcpy(ifa->ifa_label, RTA_DATA(rta[IFA_LABEL-1]), IFNAMSIZ);
	else
		memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);

	return inet_insert_ifa(in_dev, ifa);
}

#endif

/*
 *	Determine a default network mask, based on the IP address.
 */

static __inline__ int inet_abc_len(u32 addr)
{
  	if (ZERONET(addr))
  		return 0;

  	addr = ntohl(addr);
  	if (IN_CLASSA(addr))
  		return 8;
  	if (IN_CLASSB(addr))
  		return 16;
  	if (IN_CLASSC(addr))
  		return 24;

	/*
	 *	Something else, probably a multicast.
	 */

  	return -1;
}


#ifdef _HURD_

#define devinet_ioctl 0

error_t
configure_device (struct device *dev,
		  uint32_t addr, uint32_t netmask, uint32_t peer,
		  uint32_t broadcast)
{
  struct in_device *in_dev = dev->ip_ptr;
  struct in_ifaddr *ifa = in_dev ? in_dev->ifa_list : 0;

  if (ifa)
    {
      inet_del_ifa (in_dev, &in_dev->ifa_list, 0);
      ifa->ifa_broadcast = 0;
      ifa->ifa_anycast = 0;
    }
  else
    {
      ifa = inet_alloc_ifa ();
      if (!ifa)
	return ENOBUFS;
      memcpy (ifa->ifa_label, dev->name, IFNAMSIZ);

      ifa->ifa_address = INADDR_NONE;
      ifa->ifa_mask = INADDR_NONE;
      ifa->ifa_broadcast = INADDR_NONE;
      ifa->ifa_local = INADDR_NONE;
    }

  if (addr != INADDR_NONE)
    ifa->ifa_address = ifa->ifa_local = addr;
  if (netmask != INADDR_NONE && !(dev->flags & IFF_POINTOPOINT))
    {
      ifa->ifa_mask = netmask;
      ifa->ifa_prefixlen = inet_mask_len (ifa->ifa_mask);
      if ((dev->flags&IFF_BROADCAST) && ifa->ifa_prefixlen < 31)
	ifa->ifa_broadcast = ifa->ifa_address|~ifa->ifa_mask;
      else
	ifa->ifa_broadcast = 0;
    }
  if (peer != INADDR_NONE && (dev->flags & IFF_POINTOPOINT))
    {
      ifa->ifa_prefixlen = 32;
      ifa->ifa_mask = inet_make_mask(32);
      ifa->ifa_address = peer;
    }

  if (broadcast != INADDR_NONE)
    ifa->ifa_broadcast = broadcast;

  return - (inet_set_ifa (dev, ifa)
	    ?: dev_change_flags (dev, dev->flags | IFF_UP));
}

void
inquire_device (struct device *dev,
		uint32_t *addr, uint32_t *netmask, uint32_t *peer,
		uint32_t *broadcast)
{
  struct in_device *in_dev = dev->ip_ptr;
  struct in_ifaddr *ifa = in_dev ? in_dev->ifa_list : 0;

  if (ifa)
    {
      *addr = ifa->ifa_local;
      *netmask = ifa->ifa_mask;
      *peer = ifa->ifa_address;
      *broadcast = ifa->ifa_broadcast;
    }
  else
    *addr = *netmask = *peer = *broadcast = INADDR_NONE;
}

#else

int devinet_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct in_device *in_dev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	struct device *dev;
#ifdef CONFIG_IP_ALIAS
	char *colon;
#endif
	int exclusive = 0;
	int ret = 0;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;
	ifr.ifr_name[IFNAMSIZ-1] = 0;

#ifdef CONFIG_IP_ALIAS
	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;
#endif

#ifdef CONFIG_KMOD
	dev_load(ifr.ifr_name);
#endif

	switch(cmd) {
	case SIOCGIFADDR:	/* Get interface address */
	case SIOCGIFBRDADDR:	/* Get the broadcast address */
	case SIOCGIFDSTADDR:	/* Get the destination address */
	case SIOCGIFNETMASK:	/* Get the netmask for the interface */
		/* Note that this ioctls will not sleep,
		   so that we do not impose a lock.
		   One day we will be forced to put shlock here (I mean SMP)
		 */
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		break;

	case SIOCSIFFLAGS:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		rtnl_lock();
		exclusive = 1;
		break;
	case SIOCSIFADDR:	/* Set interface address (and family) */
	case SIOCSIFBRDADDR:	/* Set the broadcast address */
	case SIOCSIFDSTADDR:	/* Set the destination address */
	case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		if (sin->sin_family != AF_INET)
			return -EINVAL;
		rtnl_lock();
		exclusive = 1;
		break;
	default:
		return -EINVAL;
	}


	if ((dev = dev_get(ifr.ifr_name)) == NULL) {
		ret = -ENODEV;
		goto done;
	}

#ifdef CONFIG_IP_ALIAS
	if (colon)
		*colon = ':';
#endif

	if ((in_dev=dev->ip_ptr) != NULL) {
		for (ifap=&in_dev->ifa_list; (ifa=*ifap) != NULL; ifap=&ifa->ifa_next)
			if (strcmp(ifr.ifr_name, ifa->ifa_label) == 0)
				break;
	}

	if (ifa == NULL && cmd != SIOCSIFADDR && cmd != SIOCSIFFLAGS) {
		ret = -EADDRNOTAVAIL;
		goto done;
	}

	switch(cmd) {
		case SIOCGIFADDR:	/* Get interface address */
			sin->sin_addr.s_addr = ifa->ifa_local;
			goto rarok;

		case SIOCGIFBRDADDR:	/* Get the broadcast address */
			sin->sin_addr.s_addr = ifa->ifa_broadcast;
			goto rarok;

		case SIOCGIFDSTADDR:	/* Get the destination address */
			sin->sin_addr.s_addr = ifa->ifa_address;
			goto rarok;

		case SIOCGIFNETMASK:	/* Get the netmask for the interface */
			sin->sin_addr.s_addr = ifa->ifa_mask;
			goto rarok;

		case SIOCSIFFLAGS:
#ifdef CONFIG_IP_ALIAS
			if (colon) {
				if (ifa == NULL) {
					ret = -EADDRNOTAVAIL;
					break;
				}
				if (!(ifr.ifr_flags&IFF_UP))
					inet_del_ifa(in_dev, ifap, 1);
				break;
			}
#endif
			ret = dev_change_flags(dev, ifr.ifr_flags);
			break;

		case SIOCSIFADDR:	/* Set interface address (and family) */
			if (inet_abc_len(sin->sin_addr.s_addr) < 0) {
				ret = -EINVAL;
				break;
			}

			if (!ifa) {
				if ((ifa = inet_alloc_ifa()) == NULL) {
					ret = -ENOBUFS;
					break;
				}
#ifdef CONFIG_IP_ALIAS
				if (colon)
					memcpy(ifa->ifa_label, ifr.ifr_name, IFNAMSIZ);
				else
#endif
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
			} else {
				ret = 0;
				if (ifa->ifa_local == sin->sin_addr.s_addr)
					break;
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_broadcast = 0;
				ifa->ifa_anycast = 0;
			}

			ifa->ifa_address =
			ifa->ifa_local = sin->sin_addr.s_addr;

			if (!(dev->flags&IFF_POINTOPOINT)) {
				ifa->ifa_prefixlen = inet_abc_len(ifa->ifa_address);
				ifa->ifa_mask = inet_make_mask(ifa->ifa_prefixlen);
				if ((dev->flags&IFF_BROADCAST) && ifa->ifa_prefixlen < 31)
					ifa->ifa_broadcast = ifa->ifa_address|~ifa->ifa_mask;
			} else {
				ifa->ifa_prefixlen = 32;
				ifa->ifa_mask = inet_make_mask(32);
			}
			ret = inet_set_ifa(dev, ifa);
			break;

		case SIOCSIFBRDADDR:	/* Set the broadcast address */
			if (ifa->ifa_broadcast != sin->sin_addr.s_addr) {
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_broadcast = sin->sin_addr.s_addr;
				inet_insert_ifa(in_dev, ifa);
			}
			break;

		case SIOCSIFDSTADDR:	/* Set the destination address */
			if (ifa->ifa_address != sin->sin_addr.s_addr) {
				if (inet_abc_len(sin->sin_addr.s_addr) < 0) {
					ret = -EINVAL;
					break;
				}
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_address = sin->sin_addr.s_addr;
				inet_insert_ifa(in_dev, ifa);
			}
			break;

		case SIOCSIFNETMASK: 	/* Set the netmask for the interface */

			/*
			 *	The mask we set must be legal.
			 */
			if (bad_mask(sin->sin_addr.s_addr, 0)) {
				ret = -EINVAL;
				break;
			}

			if (ifa->ifa_mask != sin->sin_addr.s_addr) {
				inet_del_ifa(in_dev, ifap, 0);
				ifa->ifa_mask = sin->sin_addr.s_addr;
				ifa->ifa_prefixlen = inet_mask_len(ifa->ifa_mask);
				inet_set_ifa(dev, ifa);
			}
			break;
	}
done:
	if (exclusive)
		rtnl_unlock();
	return ret;

rarok:
	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}

#endif

static int
inet_gifconf(struct device *dev, char *buf, int len)
{
	struct in_device *in_dev = dev->ip_ptr;
	struct in_ifaddr *ifa;
	struct ifreq ifr;
	int done=0;

	if (in_dev==NULL || (ifa=in_dev->ifa_list)==NULL)
		return 0;

	for ( ; ifa; ifa = ifa->ifa_next) {
		if (!buf) {
			done += sizeof(ifr);
			continue;
		}
		if (len < (int) sizeof(ifr))
			return done;
		memset(&ifr, 0, sizeof(struct ifreq));
		if (ifa->ifa_label)
			strcpy(ifr.ifr_name, ifa->ifa_label);
		else
			strcpy(ifr.ifr_name, dev->name);

#ifdef _HURD_
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_len = sizeof (struct sockaddr_in);
#endif
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_family = AF_INET;
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_addr.s_addr = ifa->ifa_local;

		if (copy_to_user(buf, &ifr, sizeof(struct ifreq)))
			return -EFAULT;
		buf += sizeof(struct ifreq);
		len -= sizeof(struct ifreq);
		done += sizeof(struct ifreq);
	}
	return done;
}

u32 inet_select_addr(struct device *dev, u32 dst, int scope)
{
	u32 addr = 0;
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL)
		return 0;

	for_primary_ifa(in_dev) {
		if (ifa->ifa_scope > scope)
			continue;
		if (!dst || inet_ifa_match(dst, ifa))
			return ifa->ifa_local;
		if (!addr)
			addr = ifa->ifa_local;
	} endfor_ifa(in_dev);

	if (addr)
		return addr;

	/* Not loopback addresses on loopback should be preferred
	   in this case. It is importnat that lo is the first interface
	   in dev_base list.
	 */
	for (dev=dev_base; dev; dev=dev->next) {
		if ((in_dev=dev->ip_ptr) == NULL)
			continue;

		for_primary_ifa(in_dev) {
			if (!IN_DEV_HIDDEN(in_dev) &&
			    ifa->ifa_scope <= scope &&
			    ifa->ifa_scope != RT_SCOPE_LINK)
				return ifa->ifa_local;
		} endfor_ifa(in_dev);
	}

	return 0;
}

/*
 *	Device notifier
 */

int register_inetaddr_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&inetaddr_chain, nb);
}

int unregister_inetaddr_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&inetaddr_chain,nb);
}

static int inetdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	struct in_device *in_dev = dev->ip_ptr;

	if (in_dev == NULL)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		if (in_dev)
			printk(KERN_DEBUG "inetdev_event: bug\n");
		dev->ip_ptr = NULL;
		break;
	case NETDEV_UP:
		if (dev == &loopback_dev) {
			struct in_ifaddr *ifa;
			if ((ifa = inet_alloc_ifa()) != NULL) {
				ifa->ifa_local =
				ifa->ifa_address = htonl(INADDR_LOOPBACK);
				ifa->ifa_prefixlen = 8;
				ifa->ifa_mask = inet_make_mask(8);
				ifa->ifa_dev = in_dev;
				ifa->ifa_scope = RT_SCOPE_HOST;
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
				inet_insert_ifa(in_dev, ifa);
			}
		}
		ip_mc_up(in_dev);
		break;
	case NETDEV_DOWN:
		ip_mc_down(in_dev);
		break;
	case NETDEV_CHANGEMTU:
		if (dev->mtu >= 68)
			break;
		/* MTU falled under minimal IP mtu. Disable IP. */
	case NETDEV_UNREGISTER:
		inetdev_destroy(in_dev);
		break;
	case NETDEV_CHANGENAME:
		if (in_dev->ifa_list) {
			struct in_ifaddr *ifa;
			for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
			/* Do not notify about label change, this event is
			   not interesting to applications using netlink.
			 */
		}
		break;
	}

	return NOTIFY_DONE;
}

struct notifier_block ip_netdev_notifier={
	inetdev_event,
	NULL,
	0
};

#ifdef CONFIG_RTNETLINK

static int inet_fill_ifaddr(struct sk_buff *skb, struct in_ifaddr *ifa,
			    u32 pid, u32 seq, int event)
{
	struct ifaddrmsg *ifm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, pid, seq, event, sizeof(*ifm));
	ifm = NLMSG_DATA(nlh);
	ifm->ifa_family = AF_INET;
	ifm->ifa_prefixlen = ifa->ifa_prefixlen;
	ifm->ifa_flags = ifa->ifa_flags|IFA_F_PERMANENT;
	ifm->ifa_scope = ifa->ifa_scope;
	ifm->ifa_index = ifa->ifa_dev->dev->ifindex;
	if (ifa->ifa_address)
		RTA_PUT(skb, IFA_ADDRESS, 4, &ifa->ifa_address);
	if (ifa->ifa_local)
		RTA_PUT(skb, IFA_LOCAL, 4, &ifa->ifa_local);
	if (ifa->ifa_broadcast)
		RTA_PUT(skb, IFA_BROADCAST, 4, &ifa->ifa_broadcast);
	if (ifa->ifa_anycast)
		RTA_PUT(skb, IFA_ANYCAST, 4, &ifa->ifa_anycast);
	if (ifa->ifa_label[0])
		RTA_PUT(skb, IFA_LABEL, IFNAMSIZ, &ifa->ifa_label);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int inet_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, ip_idx;
	int s_idx, s_ip_idx;
	struct device *dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	s_idx = cb->args[0];
	s_ip_idx = ip_idx = cb->args[1];
	for (dev=dev_base, idx=0; dev; dev = dev->next, idx++) {
		if (idx < s_idx)
			continue;
		if (idx > s_idx)
			s_ip_idx = 0;
		if ((in_dev = dev->ip_ptr) == NULL)
			continue;
		for (ifa = in_dev->ifa_list, ip_idx = 0; ifa;
		     ifa = ifa->ifa_next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			if (inet_fill_ifaddr(skb, ifa, NETLINK_CB(cb->skb).pid,
					     cb->nlh->nlmsg_seq, RTM_NEWADDR) <= 0)
				goto done;
		}
	}
done:
	cb->args[0] = idx;
	cb->args[1] = ip_idx;

	return skb->len;
}

static void rtmsg_ifa(int event, struct in_ifaddr * ifa)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct ifaddrmsg)+128);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb) {
		netlink_set_err(rtnl, 0, RTMGRP_IPV4_IFADDR, ENOBUFS);
		return;
	}
	if (inet_fill_ifaddr(skb, ifa, 0, 0, event) < 0) {
		kfree_skb(skb);
		netlink_set_err(rtnl, 0, RTMGRP_IPV4_IFADDR, EINVAL);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_IPV4_IFADDR;
	netlink_broadcast(rtnl, skb, 0, RTMGRP_IPV4_IFADDR, GFP_KERNEL);
}


static struct rtnetlink_link inet_rtnetlink_table[RTM_MAX-RTM_BASE+1] =
{
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},

	{ inet_rtm_newaddr,	NULL,			},
	{ inet_rtm_deladdr,	NULL,			},
	{ NULL,			inet_dump_ifaddr,	},
	{ NULL,			NULL,			},

	{ inet_rtm_newroute,	NULL,			},
	{ inet_rtm_delroute,	NULL,			},
	{ inet_rtm_getroute,	inet_dump_fib,		},
	{ NULL,			NULL,			},

	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},

#ifdef CONFIG_IP_MULTIPLE_TABLES
	{ inet_rtm_newrule,	NULL,			},
	{ inet_rtm_delrule,	NULL,			},
	{ NULL,			inet_dump_rules,	},
	{ NULL,			NULL,			},
#else
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
	{ NULL,			NULL,			},
#endif
};

#endif /* CONFIG_RTNETLINK */


#ifdef CONFIG_SYSCTL

void inet_forward_change()
{
	struct device *dev;
	int on = ipv4_devconf.forwarding;

	ipv4_devconf.accept_redirects = !on;
	ipv4_devconf_dflt.forwarding = on;

	for (dev = dev_base; dev; dev = dev->next) {
		struct in_device *in_dev = dev->ip_ptr;
		if (in_dev)
			in_dev->cnf.forwarding = on;
	}

	rt_cache_flush(0);

	ip_statistics.IpForwarding = on ? 1 : 2;
}

static
int devinet_sysctl_forward(ctl_table *ctl, int write, struct file * filp,
			   void *buffer, size_t *lenp)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write && *valp != val) {
		if (valp == &ipv4_devconf.forwarding)
			inet_forward_change();
		else if (valp != &ipv4_devconf_dflt.forwarding)
			rt_cache_flush(0);
	}

        return ret;
}

static struct devinet_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table devinet_vars[13];
	ctl_table devinet_dev[2];
	ctl_table devinet_conf_dir[2];
	ctl_table devinet_proto_dir[2];
	ctl_table devinet_root_dir[2];
} devinet_sysctl = {
	NULL,
	{{NET_IPV4_CONF_FORWARDING, "forwarding",
         &ipv4_devconf.forwarding, sizeof(int), 0644, NULL,
         &devinet_sysctl_forward},
	{NET_IPV4_CONF_MC_FORWARDING, "mc_forwarding",
         &ipv4_devconf.mc_forwarding, sizeof(int), 0444, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_ACCEPT_REDIRECTS, "accept_redirects",
         &ipv4_devconf.accept_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_SECURE_REDIRECTS, "secure_redirects",
         &ipv4_devconf.secure_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_SHARED_MEDIA, "shared_media",
         &ipv4_devconf.shared_media, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_RP_FILTER, "rp_filter",
         &ipv4_devconf.rp_filter, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_SEND_REDIRECTS, "send_redirects",
         &ipv4_devconf.send_redirects, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_ACCEPT_SOURCE_ROUTE, "accept_source_route",
         &ipv4_devconf.accept_source_route, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_PROXY_ARP, "proxy_arp",
         &ipv4_devconf.proxy_arp, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_BOOTP_RELAY, "bootp_relay",
         &ipv4_devconf.bootp_relay, sizeof(int), 0644, NULL,
         &proc_dointvec},
        {NET_IPV4_CONF_LOG_MARTIANS, "log_martians",
         &ipv4_devconf.log_martians, sizeof(int), 0644, NULL,
         &proc_dointvec},
	{NET_IPV4_CONF_HIDDEN, "hidden",
         &ipv4_devconf.hidden, sizeof(int), 0644, NULL,
         &proc_dointvec},
	 {0}},

	{{NET_PROTO_CONF_ALL, "all", NULL, 0, 0555, devinet_sysctl.devinet_vars},{0}},
	{{NET_IPV4_CONF, "conf", NULL, 0, 0555, devinet_sysctl.devinet_dev},{0}},
	{{NET_IPV4, "ipv4", NULL, 0, 0555, devinet_sysctl.devinet_conf_dir},{0}},
	{{CTL_NET, "net", NULL, 0, 0555, devinet_sysctl.devinet_proto_dir},{0}}
};

static void devinet_sysctl_register(struct in_device *in_dev, struct ipv4_devconf *p)
{
	int i;
	struct device *dev = in_dev ? in_dev->dev : NULL;
	struct devinet_sysctl_table *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;
	memcpy(t, &devinet_sysctl, sizeof(*t));
	for (i=0; i<sizeof(t->devinet_vars)/sizeof(t->devinet_vars[0])-1; i++) {
		t->devinet_vars[i].data += (char*)p - (char*)&ipv4_devconf;
		t->devinet_vars[i].de = NULL;
	}
	if (dev) {
		t->devinet_dev[0].procname = dev->name;
		t->devinet_dev[0].ctl_name = dev->ifindex;
	} else {
		t->devinet_dev[0].procname = "default";
		t->devinet_dev[0].ctl_name = NET_PROTO_CONF_DEFAULT;
	}
	t->devinet_dev[0].child = t->devinet_vars;
	t->devinet_dev[0].de = NULL;
	t->devinet_conf_dir[0].child = t->devinet_dev;
	t->devinet_conf_dir[0].de = NULL;
	t->devinet_proto_dir[0].child = t->devinet_conf_dir;
	t->devinet_proto_dir[0].de = NULL;
	t->devinet_root_dir[0].child = t->devinet_proto_dir;
	t->devinet_root_dir[0].de = NULL;

	t->sysctl_header = register_sysctl_table(t->devinet_root_dir, 0);
	if (t->sysctl_header == NULL)
		kfree(t);
	else
		p->sysctl = t;
}

static void devinet_sysctl_unregister(struct ipv4_devconf *p)
{
	if (p->sysctl) {
		struct devinet_sysctl_table *t = p->sysctl;
		p->sysctl = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t);
	}
}
#endif

__initfunc(void devinet_init(void))
{
	register_gifconf(PF_INET, inet_gifconf);
	register_netdevice_notifier(&ip_netdev_notifier);
#ifdef CONFIG_RTNETLINK
	rtnetlink_links[PF_INET] = inet_rtnetlink_table;
#endif
#ifdef CONFIG_SYSCTL
	devinet_sysctl.sysctl_header =
		register_sysctl_table(devinet_sysctl.devinet_root_dir, 0);
	devinet_sysctl_register(NULL, &ipv4_devconf_dflt);
#endif
}
