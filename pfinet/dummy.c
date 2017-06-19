/*
   Copyright (C) 1995,96,98,99,2000 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include <device/device.h>
#include <device/net_status.h>
#include <netinet/in.h>
#include <string.h>
#include <error.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>

struct dummy_device
{
  struct dummy_device *next;
  struct device dev;
  struct net_device_stats stats;
};

/* Linked list of all dummy devices.  */
struct dummy_device *dummy_dev;

struct net_device_stats *
dummy_get_stats (struct device *dev)
{
  struct dummy_device *ddev = (struct dummy_device *) dev->priv;
  return &ddev->stats;
}

int
dummy_stop (struct device *dev)
{
  return 0;
}

void
dummy_set_multi (struct device *dev)
{
}

int
dummy_open (struct device *dev)
{
  return 0;
}

int
dummy_xmit (struct sk_buff *skb, struct device *dev)
{
  struct dummy_device *ddev = (struct dummy_device *) dev->priv;

  ddev->stats.tx_packets++;
  ddev->stats.tx_bytes += skb->len;

  dev_kfree_skb (skb);
  return 0;
}

void
setup_dummy_device (char *name, struct device **device)
{
  error_t err;
  struct dummy_device *ddev;
  struct device *dev;

  ddev = calloc (1, sizeof (struct dummy_device));
  if (!ddev)
    error (2, ENOMEM, "%s", name);
  ddev->next = dummy_dev;
  dummy_dev = ddev;

  *device = dev = &ddev->dev;

  dev->name = strdup (name);

  dev->priv = ddev;
  dev->get_stats = dummy_get_stats;

  dev->open = dummy_open;
  dev->stop = dummy_stop;
  dev->hard_start_xmit = dummy_xmit;
  dev->set_multicast_list = dummy_set_multi;

  /* These are the ones set by drivers/net/net_init.c::ether_setup.  */
  dev->hard_header = eth_header;
  dev->rebuild_header = eth_rebuild_header;
  dev->hard_header_cache = eth_header_cache;
  dev->header_cache_update = eth_header_cache_update;
  dev->hard_header_parse = eth_header_parse;
  /* We can't do these two (and we never try anyway).  */
  /* dev->change_mtu = eth_change_mtu; */
  /* dev->set_mac_address = eth_mac_addr; */

  /* Some more fields */
  dev->type = ARPHRD_ETHER;
  dev->hard_header_len = ETH_HLEN;
  dev->addr_len = ETH_ALEN;
  memset (dev->broadcast, 0xff, ETH_ALEN);
  dev->flags = IFF_BROADCAST | IFF_MULTICAST;
  dev_init_buffers (dev);

  dev->mtu = 1500;
  dev->tx_queue_len = 0;
  dev->flags |= IFF_NOARP;
  dev->flags &= ~IFF_MULTICAST;

  /* That should be enough.  */

  /* This call adds the device to the `dev_base' chain,
     initializes its `ifindex' member (which matters!),
     and tells the protocol stacks about the device.  */
  err = - register_netdevice (dev);
  assert_perror_backtrace (err);
}
