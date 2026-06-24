/*
   Copyright (C) 2000, 2007, 2017 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Ioctls for network device configuration */

#include <lwip_iioctl_S.h>
#include <lwip_rioctl_S.h>

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <device/device.h>
#include <device/net_status.h>

#include <lwip-hurd.h>
#include <lwip-util.h>
#include <lwip/tcpip.h>
#include <netif/ifcommon.h>
#include <netinet/in.h>
#include <hurd/ioctl_types.h>

/* Get the interface from its name */
static struct netif *
get_if (const char *name)
{
  char ifname[IFNAMSIZ];
  struct netif *netif;

  strncpy (ifname, name, IFNAMSIZ - 1);
  ifname[IFNAMSIZ - 1] = 0;

  NETIF_FOREACH(netif)
    {
      if (strcmp (netif_get_state (netif)->devname, ifname) == 0)
	break;
    }

  return netif;
}

enum siocgif_type
{
  ADDR,
  NETMASK,
  DSTADDR,
  BRDADDR,
  GWADDR,
};

#define SIOCGIF(name, type)						\
  kern_return_t								\
  lwip_S_iioctl_siocgif##name (struct sock_user *user,                       \
        ifname_t ifnam,				\
        sockaddr_t *addr)				\
  {									\
    return siocgifXaddr (user, ifnam, addr, type);			\
  }

/* Get some sockaddr type of info.  */
static kern_return_t
siocgifXaddr (struct sock_user *user,
	      ifname_t ifnam, sockaddr_t * addr, enum siocgif_type type)
{
  kern_return_t err = 0;
  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  size_t buflen = sizeof (struct sockaddr);
  struct netif *netif;
  ip4_addr_t addrs[5];

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (ifnam);
  if (!netif)
    return ENODEV;

  if (type == DSTADDR)
    return EOPNOTSUPP;

  /* We're only interested in geting the address family */
  err = lwip_getsockname (user->sock->sockno, addr, (socklen_t *) & buflen);
  if (err)
    return err;

  if (sin->sin_family != AF_INET)
    err = EINVAL;
  else
    {
      inquire_device (netif, &addrs[ADDR], &addrs[NETMASK], &addrs[DSTADDR],
		      &addrs[BRDADDR], &addrs[GWADDR], 0, 0);
      sin->sin_addr.s_addr = addrs[type].addr;
    }

  return err;
}

#define SIOCSIF(name, type)						\
  kern_return_t								\
  lwip_S_iioctl_siocsif##name (struct sock_user *user,                       \
			  const ifname_t ifnam,				\
			  sockaddr_t addr)				\
  {									\
    return siocsifXaddr (user, ifnam, &addr, type);			\
  }

/* Set some sockaddr type of info.  */
static kern_return_t
siocsifXaddr (struct sock_user *user,
	      const ifname_t ifnam, sockaddr_t * addr, enum siocgif_type type)
{
  kern_return_t err = 0;
  struct sockaddr_in sin;
  size_t buflen = sizeof (struct sockaddr_in);
  struct netif *netif;
  ip4_addr_t ipv4_addrs[5];

  if (!user)
    return EOPNOTSUPP;

  if (addr->sa_family != AF_INET)
    return EINVAL;

  if (!user->isroot)
    return EPERM;

  netif = get_if (ifnam);

  if (!netif)
    return ENODEV;

  if (type == DSTADDR || type == BRDADDR)
    return EOPNOTSUPP;

  err = lwip_getsockname (user->sock->sockno,
			  (sockaddr_t *) & sin, (socklen_t *) & buflen);
  if (err)
    return err;

  if (sin.sin_family != AF_INET)
    err = EINVAL;
  else
    {
      inquire_device (netif, &ipv4_addrs[ADDR], &ipv4_addrs[NETMASK],
		      &ipv4_addrs[DSTADDR], &ipv4_addrs[BRDADDR],
		      &ipv4_addrs[GWADDR], 0, 0);

      ipv4_addrs[type].addr = ((struct sockaddr_in *) addr)->sin_addr.s_addr;

      err = configure_device (netif, ipv4_addrs[ADDR], ipv4_addrs[NETMASK],
			      ipv4_addrs[DSTADDR], ipv4_addrs[BRDADDR],
			      ipv4_addrs[GWADDR], 0);
    }

  return err;
}

static void
clear_gateways (void *arg)
{
  struct netif *netif;
  struct ip4_addr gw;

  gw.addr = INADDR_NONE;
  NETIF_FOREACH (netif)
  {
    netif_set_gw (netif, &gw);
  }
}

static void
set_default_if (void *arg)
{
  struct netif *netif;

  netif = (struct netif *) arg;

  netif_set_default (netif);
}

/* 10 SIOCADDRT -- Add a network route */
/*
 * Lwip routing is very limited. Each netif has one gateway and all packets from/to that netif go through there.
 * Considering this, we need to behave as clients expect.
 *
 * These are the supported scenarios:
 *   - A client sending an interface plus a netmask but gateway=any: intends to add a subnet route.
 *     e.g. `192.168.1.0/24 dev eth0`
 *   - A client sending an interface plus a gateway but netmask=any: intends to set a default gateway.
 *     e.g. `0.0.0.0/0 via 192.168.1.1`
 */
kern_return_t
lwip_S_rioctl_siocaddrt (struct sock_user *user,
			 const ifname_t ifnam, const struct srtentry route)
{
  kern_return_t err = 0;
  struct netif *netif;
  struct sockaddr sa;
  size_t buflen = sizeof (struct sockaddr);
  ip4_addr_t ipv4_addrs[5];

  if (!user)
    return EOPNOTSUPP;

  if (!user->isroot)
    return EPERM;

  /* All ones netmask means host route, not supported by lwip */
  if (route.rt_mask == INADDR_NONE)
    return EOPNOTSUPP;

  netif = get_if (ifnam);
  if (!netif)
    return ENODEV;

  err = lwip_getsockname (user->sock->sockno, &sa, (socklen_t *)&buflen);
  if (err)
    return err;

  if (sa.sa_family != AF_INET)
    return EINVAL;

  inquire_device (netif, &ipv4_addrs[ADDR], &ipv4_addrs[NETMASK],
		  &ipv4_addrs[DSTADDR], &ipv4_addrs[BRDADDR],
		  &ipv4_addrs[GWADDR], 0, 0);

  if (route.rt_mask != INADDR_ANY && route.rt_gateway == INADDR_ANY)
    {
      /*
       * Subnet route.
       * Only one network can go through the interface so we set the netmask to the interface.
       */

      /* masking current IP must match given dest to be valid */
      if (ipv4_addrs[ADDR].addr != INADDR_ANY
	  && ipv4_addrs[ADDR].addr != INADDR_NONE
	  && (ipv4_addrs[ADDR].addr & route.rt_mask) != route.rt_dest)
	return ENETUNREACH;

      ipv4_addrs[NETMASK].addr = route.rt_mask;
    }
  else if (route.rt_gateway != INADDR_ANY)
    {
      /*
       * Netmask is any, and we got a gateway so client is trying to add a default route.
       * We set the given gateway to the given interface and set the interface as default.
       */

      /* First we verify the gateway is reachable from this netif */
      if (ipv4_addrs[ADDR].addr != INADDR_ANY
	  && ipv4_addrs[ADDR].addr != INADDR_NONE
	  && ipv4_addrs[NETMASK].addr != INADDR_ANY
	  && ipv4_addrs[NETMASK].addr != INADDR_NONE
	  && (route.rt_gateway & ipv4_addrs[NETMASK].addr) !=
	  (ipv4_addrs[ADDR].addr & ipv4_addrs[NETMASK].addr))
	return EHOSTUNREACH;

      /*
       * Since we only allow setting a gateway when it will become the default gateway,
       * any existing gateway must have been previously set as the default. However, there
       * can only be one default gateway at a time, so we must clear any existing gateways
       * before setting the new one.
       */
      tcpip_callback (clear_gateways, NULL);

      ipv4_addrs[GWADDR].addr = route.rt_gateway;
      tcpip_callback (set_default_if, netif);
    }
  else
    {
      /* Any  other scenario not supported */
      return EOPNOTSUPP;
    }

  err = configure_device (netif, ipv4_addrs[ADDR], ipv4_addrs[NETMASK],
			  ipv4_addrs[DSTADDR], ipv4_addrs[BRDADDR],
			  ipv4_addrs[GWADDR], 0);

  return err;
}

/* 11 SIOCDELRT -- Delete a network route */
/*
 * The only routing lwip supports is the default gateway for each netif.
 * We interpret "deleting a route" as removing the current gateway and netmask,
 * but only if the given route matches.
 *
 * Supported scenarios:
 *   - A client sending an interface plus a netmask but gateway=any: intends to remove a subnet route.
 *     e.g. `192.168.1.0/24 dev eth0`
 *   - A client sending an interface plus a gateway but netmask=any: intends to remove a default gateway.
 *     e.g. `0.0.0.0/0 via 192.168.1.1`
 */
kern_return_t
lwip_S_rioctl_siocdelrt (struct sock_user *user,
			 const ifname_t ifnam, const struct srtentry route)
{
  kern_return_t err = 0;
  struct netif *netif;
  struct sockaddr sa;
  size_t buflen = sizeof (struct sockaddr);
  ip4_addr_t ipv4_addrs[5];

  if (!user)
    return EOPNOTSUPP;

  if (!user->isroot)
    return EPERM;

  netif = get_if (ifnam);
  if (!netif)
    return ENODEV;

  err = lwip_getsockname (user->sock->sockno, &sa, (socklen_t *)&buflen);
  if (err)
    return err;

  if (sa.sa_family != AF_INET)
    return EINVAL;

  inquire_device (netif, &ipv4_addrs[ADDR], &ipv4_addrs[NETMASK],
		  &ipv4_addrs[DSTADDR], &ipv4_addrs[BRDADDR],
		  &ipv4_addrs[GWADDR], 0, 0);

  if (route.rt_mask != INADDR_ANY && route.rt_gateway == INADDR_ANY)
    {
      /*
       * Subnet route.
       * Only one network can go through the interface so we remove the netmask from the interface.
       */

      /* We remove the netmask only if it matches the given one */
      if (ipv4_addrs[NETMASK].addr != INADDR_ANY
	  && ipv4_addrs[NETMASK].addr != INADDR_NONE
	  && ipv4_addrs[NETMASK].addr != route.rt_mask)
	return EINVAL;

      ipv4_addrs[NETMASK].addr = INADDR_NONE;
    }
  else if (route.rt_gateway != INADDR_ANY)
    {
      /*
       * Netmask is any, and we got a gateway so client is trying to remove a default route.
       * We remove the gateway from the given interface.
       */

      /* We remove the gateway only if it matches the given one */
      if (ipv4_addrs[GWADDR].addr != INADDR_ANY
	  && ipv4_addrs[GWADDR].addr != INADDR_NONE
	  && ipv4_addrs[GWADDR].addr != route.rt_gateway)
	return EINVAL;

      /* And only if it was the default one */
      if (netif != netif_default)
	return EINVAL;

      ipv4_addrs[GWADDR].addr = INADDR_NONE;
    }
  else
    {
      /* Any  other scenario not supported */
      return EOPNOTSUPP;
    }

  err = configure_device (netif, ipv4_addrs[ADDR], ipv4_addrs[NETMASK],
			  ipv4_addrs[DSTADDR], ipv4_addrs[BRDADDR],
			  ipv4_addrs[GWADDR], 0);

  return err;
}

/* 12 SIOCSIFADDR -- Set address of a network interface.  */
SIOCSIF (addr, ADDR);

/* 14 SIOCSIFDSTADDR -- Set point-to-point (peer) address of a network interface.  */
SIOCSIF (dstaddr, DSTADDR);

/* 16 SIOCSIFFLAGS -- Set flags of a network interface.  */
kern_return_t
lwip_S_iioctl_siocsifflags (struct sock_user * user,
			    const ifname_t ifnam,
			    short flags)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (ifnam);

  if (!user->isroot)
    err = EPERM;
  else if (!netif)
    err = ENODEV;
  else
    err = if_change_flags (netif, flags);

  return err;
}

/* 17 SIOCGIFFLAGS -- Get flags of a network interface.  */
kern_return_t
lwip_S_iioctl_siocgifflags (struct sock_user * user, ifname_t name, short *flags)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (name);
  if (!netif)
    err = ENODEV;
  else
    {
      *flags = netif_get_state (netif)->flags;
    }

  return err;
}

/* 19 SIOCSIFBRDADDR -- Set broadcast address of a network interface.  */
SIOCSIF (brdaddr, BRDADDR);

/* 22 SIOCSIFNETMASK -- Set netmask of a network interface.  */
SIOCSIF (netmask, NETMASK);

/* 23 SIOCGIFMETRIC -- Get metric of a network interface.  */
kern_return_t
lwip_S_iioctl_siocgifmetric (struct sock_user * user,
			     ifname_t ifnam,
			     int *metric)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (ifnam);
  if (!netif)
    err = ENODEV;
  else
    {
      *metric = 0;		/* Not supported.  */
    }

  return err;
}

/* 24 SIOCSIFMETRIC -- Set metric of a network interface.  */
kern_return_t
lwip_S_iioctl_siocsifmetric (struct sock_user * user,
			     const ifname_t ifnam,
			     int metric)
{
  return EOPNOTSUPP;
}

/* 25 SIOCDIFADDR -- Delete interface address.  */
kern_return_t
lwip_S_iioctl_siocdifaddr (struct sock_user * user,
			   const ifname_t ifnam,
			   sockaddr_t addr)
{
  /* To delete an address, we set it to ADDR_NONE.
   * That will remove the netmask and the gateway as well.
   */
  struct sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_NONE;

  return siocsifXaddr (user, ifnam, (struct sockaddr *) &sin, ADDR);
}

/* 33 SIOCGIFADDR -- Get address of a network interface.  */
SIOCGIF (addr, ADDR);

/* 34 SIOCGIFDSTADDR -- Get point-to-point address of a network interface.  */
SIOCGIF (dstaddr, DSTADDR);

/* 35 SIOCGIFBRDADDR -- Get broadcast address of a network interface.  */
SIOCGIF (brdaddr, BRDADDR);

/* 37 SIOCGIFNETMASK -- Get netmask of a network interface.  */
SIOCGIF (netmask, NETMASK);

/* 39 SIOCGIFHWADDR -- Get the hardware address of a network interface.  */
kern_return_t
lwip_S_iioctl_siocgifhwaddr (struct sock_user * user,
			     ifname_t ifname,
			     sockaddr_t * addr)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (ifname);
  if (!netif)
    err = ENODEV;
  else
    {
      memcpy (addr->sa_data, netif->hwaddr, netif->hwaddr_len);
      addr->sa_len = netif->hwaddr_len;
      addr->sa_family = netif_get_state (netif)->type;
    }

  return err;
}

/* 51 SIOCGIFMTU -- Get mtu of a network interface.  */
kern_return_t
lwip_S_iioctl_siocgifmtu (struct sock_user * user, ifname_t ifnam, int *mtu)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  netif = get_if (ifnam);
  if (!netif)
    err = ENODEV;
  else
    {
      *mtu = netif->mtu;
    }

  return err;
}

/* 51 SIOCSIFMTU -- Set mtu of a network interface.  */
kern_return_t
lwip_S_iioctl_siocsifmtu (struct sock_user * user, const ifname_t ifnam, int mtu)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  if (!user->isroot)
    return EPERM;

  if (mtu <= 0)
    return EINVAL;

  netif = get_if (ifnam);
  if (!netif)
    err = ENODEV;
  else
    {
      err = netif_get_state (netif)->update_mtu (netif, mtu);
    }

  return err;
}

/* 100 SIOCGIFINDEX -- Get index number of a network interface.  */
kern_return_t
lwip_S_iioctl_siocgifindex (struct sock_user * user,
			    ifname_t ifnam,
			    int *index)
{
  kern_return_t err = 0;
  struct netif *netif;

  if (!user)
    return EOPNOTSUPP;

  NETIF_FOREACH(netif)
    {
      if (strcmp (netif_get_state (netif)->devname, ifnam) == 0)
	{
	  *index = netif_get_index (netif);
	  break;
	}
    }

  if (!netif)
    err = ENODEV;

  return err;
}

/* 101 SIOCGIFNAME -- Get name of a network interface from index number.  */
kern_return_t
lwip_S_iioctl_siocgifname (struct sock_user * user,
			   ifname_t ifnam,
			   int *index)
{
  kern_return_t err = 0;
  struct netif *netif;
  int i;

  if (!user)
    return EOPNOTSUPP;

  if (*index < 0)
    return EINVAL;

  i = 1;			/* The first index is 1 */
  NETIF_FOREACH(netif)
    {
      if (i == *index)
	break;

      i++;
    }

  if (!netif)
    err = ENODEV;
  else
    {
      strncpy (ifnam, netif_get_state (netif)->devname, IFNAMSIZ);
      ifnam[IFNAMSIZ - 1] = '\0';
    }

  return err;
}
