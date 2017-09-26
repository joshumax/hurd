/*
   Copyright (C) 2000, 2007 Free Software Foundation, Inc.
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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "pfinet.h"

#include <linux/netdevice.h>
#include <linux/notifier.h>

#include "iioctl_S.h"
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <mach/notify.h>
#include <sys/mman.h>
#include <hurd/fshelp.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <net/sock.h>

extern struct notifier_block *netdev_chain;

/* devinet.c */
extern error_t configure_device (struct device *dev, uint32_t addr,
                                 uint32_t netmask, uint32_t peer,
                                 uint32_t broadcast);
extern void inquire_device (struct device *dev, uint32_t *addr,
                            uint32_t *netmask, uint32_t *peer,
			    uint32_t *broadcast);

/* Truncate name, take the global lock and find device with this name.  */
struct device *get_dev (char *name)
{
  char ifname[IFNAMSIZ];
  struct device *dev;

  memcpy (ifname, name, IFNAMSIZ-1);
  ifname[IFNAMSIZ-1] = 0;

  pthread_mutex_lock (&global_lock);

  for (dev = dev_base; dev; dev = dev->next)
    if (strcmp (dev->name, ifname) == 0)
      break;

  return dev;
}

enum siocgif_type
{
  ADDR,
  NETMASK,
  DSTADDR,
  BRDADDR
};

#define SIOCGIF(name, type)						\
  kern_return_t								\
  S_iioctl_siocgif##name (struct sock_user *user,                       \
			  ifname_t ifnam,				\
			  sockaddr_t *addr)				\
  {									\
    return siocgifXaddr (user, ifnam, addr, type);			\
  }

/* Get some sockaddr type of info.  */
static kern_return_t
siocgifXaddr (struct sock_user *user,
	      ifname_t ifnam,
	      sockaddr_t *addr,
	      enum siocgif_type type)
{
  error_t err = 0;
  struct device *dev;
  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  uint32_t addrs[4];

  if (!user)
    return EOPNOTSUPP;

  dev = get_dev (ifnam);
  if (!dev)
    err = ENODEV;
  else if (user->sock->sk->family != AF_INET)
    err = EINVAL;
  else
    {
      sin->sin_family = AF_INET;

      inquire_device (dev, &addrs[0], &addrs[1], &addrs[2], &addrs[3]);
      sin->sin_addr.s_addr = addrs[type];
    }

  pthread_mutex_unlock (&global_lock);
  return err;
}

#define SIOCSIF(name, type)						\
  kern_return_t								\
  S_iioctl_siocsif##name (struct sock_user *user,                       \
			  ifname_t ifnam,				\
			  sockaddr_t addr)				\
  {									\
    return siocsifXaddr (user, ifnam, &addr, type);			\
  }

/* Set some sockaddr type of info.  */
static kern_return_t
siocsifXaddr (struct sock_user *user,
	      ifname_t ifnam,
	      sockaddr_t *addr,
	      enum siocgif_type type)
{
  error_t err = 0;
  struct device *dev;
  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  uint32_t addrs[4];

  if (!user)
    return EOPNOTSUPP;

  dev = get_dev (ifnam);

  if (!user->isroot)
    err = EPERM;
  else if (!dev)
    err = ENODEV;
  else if (sin->sin_family != AF_INET)
    err = EINVAL;
  else if (user->sock->sk->family != AF_INET)
    err = EINVAL;
  else
    {
      inquire_device (dev, &addrs[0], &addrs[1], &addrs[2], &addrs[3]);
      addrs[type] = sin->sin_addr.s_addr;
      err = configure_device (dev, addrs[0], addrs[1], addrs[2], addrs[3]);
    }

  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 12 SIOCSIFADDR -- Set address of a network interface.  */
SIOCSIF (addr, ADDR);

/* 14 SIOCSIFDSTADDR -- Set point-to-point (peer) address of a network interface.  */
SIOCSIF (dstaddr, DSTADDR);

/* 16 SIOCSIFFLAGS -- Set flags of a network interface.  */
kern_return_t
S_iioctl_siocsifflags (struct sock_user *user,
		       ifname_t ifnam,
		       short flags)
{
  error_t err = 0;
  struct device *dev;

  if (!user)
    return EOPNOTSUPP;

  dev = get_dev (ifnam);

  if (!user->isroot)
    err = EPERM;
  else if (!dev)
    err = ENODEV;
  else
    err = dev_change_flags (dev, flags);

  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 17 SIOCGIFFLAGS -- Get flags of a network interface.  */
kern_return_t
S_iioctl_siocgifflags (struct sock_user *user,
		       char *name,
		       short *flags)
{
  error_t err = 0;
  struct device *dev;

  dev = get_dev (name);
  if (!dev)
    err = ENODEV;
  else
    {
      *flags = dev->flags;
    }
  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 19 SIOCSIFBRDADDR -- Set broadcast address of a network interface.  */
SIOCSIF (brdaddr, BRDADDR);

/* 22 SIOCSIFNETMASK -- Set netmask of a network interface.  */
SIOCSIF (netmask, NETMASK);

/* 23 SIOCGIFMETRIC -- Get metric of a network interface.  */
kern_return_t
S_iioctl_siocgifmetric (struct sock_user *user,
		        ifname_t ifnam,
			int *metric)
{
  error_t err = 0;
  struct device *dev;

  dev = get_dev (ifnam);
  if (!dev)
    err = ENODEV;
  else
    {
      *metric = 0; /* Not supported.  */
    }
  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 24 SIOCSIFMETRIC -- Set metric of a network interface.  */
kern_return_t
S_iioctl_siocsifmetric (struct sock_user *user,
			ifname_t ifnam,
			int metric)
{
  return EOPNOTSUPP;
}

/* 25 SIOCDIFADDR -- Delete interface address.  */
kern_return_t
S_iioctl_siocdifaddr (struct sock_user *user,
		      ifname_t ifnam,
		      sockaddr_t addr)
{
  return EOPNOTSUPP;
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
error_t
S_iioctl_siocgifhwaddr (struct sock_user *user,
			ifname_t ifname,
			sockaddr_t *addr)
{
  error_t err = 0;
  struct device *dev;

  if (!user)
    return EOPNOTSUPP;

  dev = get_dev (ifname);
  if (!dev)
    err = ENODEV;
  else
    {
      memcpy (addr->sa_data, dev->dev_addr, dev->addr_len);
      addr->sa_family = dev->type;
    }
  
  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 51 SIOCGIFMTU -- Get mtu of a network interface.  */
error_t
S_iioctl_siocgifmtu (struct sock_user *user,
		     ifname_t ifnam,
		     int *mtu)
{
  error_t err = 0;
  struct device *dev;

  dev = get_dev (ifnam);
  if (!dev)
    err = ENODEV;
  else
    {
      *mtu = dev->mtu;
    }
  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 51 SIOCSIFMTU -- Set mtu of a network interface.  */
error_t
S_iioctl_siocsifmtu (struct sock_user *user,
		     ifname_t ifnam,
		     int mtu)
{
  error_t err = 0;
  struct device *dev;

  if (!user)
    return EOPNOTSUPP;

  dev = get_dev (ifnam);

  if (!user->isroot)
    err = EPERM;
  if (!dev)
    err = ENODEV;
  else if (mtu <= 0)
    err = EINVAL;
  else
    {
      if (dev->change_mtu)
	dev->change_mtu (dev, mtu);
      else
	dev->mtu = mtu;

      notifier_call_chain (&netdev_chain, NETDEV_CHANGEMTU, dev);
    }

  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 100 SIOCGIFINDEX -- Get index number of a network interface.  */
error_t
S_iioctl_siocgifindex (struct sock_user *user,
		       ifname_t ifnam,
		       int *index)
{
  error_t err = 0;
  struct device *dev;

  dev = get_dev (ifnam);
  if (!dev)
    err = ENODEV;
  else
    {
      *index = dev->ifindex;
    }
  pthread_mutex_unlock (&global_lock);
  return err;
}

/* 101 SIOCGIFNAME -- Get name of a network interface from index number.  */
error_t
S_iioctl_siocgifname (struct sock_user *user,
		      ifname_t ifnam,
		      int *index)
{
  error_t err = 0;
  struct device *dev;

  pthread_mutex_lock (&global_lock);
  dev = dev_get_by_index (*index);
  if (!dev)
    err = ENODEV;
  else
    {
      strncpy (ifnam, dev->name, IFNAMSIZ);
      ifnam[IFNAMSIZ-1] = '\0';
    }
  pthread_mutex_unlock (&global_lock);

  return err;
}
