/*
   Copyright (C) 1995,96,98,99,2000,02 Free Software Foundation, Inc.
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


struct port_class *etherreadclass;

struct ether_device
{
  struct ether_device *next;
  device_t ether_port;
  struct port_info *readpt;
  mach_port_t readptname;
  struct device dev;
};

/* Linked list of all ethernet devices.  */
struct ether_device *ether_dev;

struct enet_statistics retbuf;


/* Mach doesn't provide this.  DAMN. */
struct enet_statistics *
ethernet_get_stats (struct device *dev)
{
  return &retbuf;
}

int
ethernet_stop (struct device *dev)
{
  return 0;
}

void
ethernet_set_multi (struct device *dev)
{
}

static short ether_filter[] =
{
#ifdef NETF_IN
  /* We have to tell the packet filtering code that we're interested in
     incoming packets.  */
  NETF_IN, /* Header.  */
#endif
  NETF_PUSHLIT | NETF_NOP,
  1
};
static int ether_filter_len = sizeof (ether_filter) / sizeof (short);

static struct port_bucket *etherport_bucket;


static any_t
ethernet_thread (any_t arg)
{
  ports_manage_port_operations_one_thread (etherport_bucket,
					   ethernet_demuxer,
					   0);
  return 0;
}

int
ethernet_demuxer (mach_msg_header_t *inp,
		  mach_msg_header_t *outp)
{
  struct net_rcv_msg *msg = (struct net_rcv_msg *) inp;
  struct sk_buff *skb;
  int datalen;
  struct ether_device *edev;
  struct device *dev = 0;

  if (inp->msgh_id != NET_RCV_MSG_ID)
    return 0;

  for (edev = ether_dev; edev; edev = edev->next)
    if (inp->msgh_local_port == edev->readptname)
      dev = &edev->dev;

  if (! dev)
    {
      if (inp->msgh_remote_port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
      return 1;
    }

  datalen = ETH_HLEN
    + msg->packet_type.msgt_number - sizeof (struct packet_header);

  __mutex_lock (&net_bh_lock);
  skb = alloc_skb (datalen, GFP_ATOMIC);
  skb->len = datalen;
  skb->dev = dev;

  /* Copy the two parts of the frame into the buffer. */
  bcopy (msg->header, skb->data, ETH_HLEN);
  bcopy (msg->packet + sizeof (struct packet_header),
	 skb->data + ETH_HLEN,
	 datalen - ETH_HLEN);

  /* Drop it on the queue. */
  skb->protocol = eth_type_trans (skb, dev);
  netif_rx (skb);
  __mutex_unlock (&net_bh_lock);

  return 1;
}


void
ethernet_initialize (void)
{
  etherport_bucket = ports_create_bucket ();
  etherreadclass = ports_create_class (0, 0);

  cthread_detach (cthread_fork (ethernet_thread, 0));
}

int
ethernet_open (struct device *dev)
{
  error_t err;
  device_t master_device;
  struct ether_device *edev = (struct ether_device *) dev->priv;

  assert (edev->ether_port == MACH_PORT_NULL);

  err = ports_create_port (etherreadclass, etherport_bucket,
			   sizeof (struct port_info), &edev->readpt);
  assert_perror (err);
  edev->readptname = ports_get_right (edev->readpt);
  mach_port_insert_right (mach_task_self (), edev->readptname, edev->readptname,
			  MACH_MSG_TYPE_MAKE_SEND);

  mach_port_set_qlimit (mach_task_self (), edev->readptname, MACH_PORT_QLIMIT_MAX);

  err = get_privileged_ports (0, &master_device);
  if (err)
    error (2, err, "cannot get device master port");

  err = device_open (master_device, D_WRITE | D_READ, dev->name, &edev->ether_port);
  mach_port_deallocate (mach_task_self (), master_device);
  if (err)
    error (2, err, "%s", dev->name);

  err = device_set_filter (edev->ether_port, ports_get_right (edev->readpt),
			   MACH_MSG_TYPE_MAKE_SEND, 0,
			   ether_filter, ether_filter_len);
  if (err)
    error (2, err, "%s", dev->name);
  return 0;
}


/* Transmit an ethernet frame */
int
ethernet_xmit (struct sk_buff *skb, struct device *dev)
{
  error_t err;
  struct ether_device *edev = (struct ether_device *) dev->priv;
  u_int count;

  err = device_write (edev->ether_port, D_NOWAIT, 0, skb->data, skb->len, &count);
  assert_perror (err);
  assert (count == skb->len);
  dev_kfree_skb (skb);
  return 0;
}

void
setup_ethernet_device (char *name, struct device **device)
{
  struct net_status netstat;
  size_t count;
  int net_address[2];
  error_t err;
  struct ether_device *edev;
  struct device *dev;

  edev = calloc (1, sizeof (struct ether_device));
  if (!edev)
    error (2, ENOMEM, "%s", name);
  edev->next = ether_dev;
  ether_dev = edev;

  *device = dev = &edev->dev;

  dev->name = strdup (name);
  /* Functions.  These ones are the true "hardware layer" in Linux.  */
  dev->open = 0;		/* We set up before calling dev_open.  */
  dev->stop = ethernet_stop;
  dev->hard_start_xmit = ethernet_xmit;
  dev->get_stats = ethernet_get_stats;
  dev->set_multicast_list = ethernet_set_multi;

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
  dev->priv = edev;         /* For reverse lookup.  */
  dev->type = ARPHRD_ETHER;
  dev->hard_header_len = ETH_HLEN;
  dev->addr_len = ETH_ALEN;
  memset (dev->broadcast, 0xff, ETH_ALEN);
  dev->flags = IFF_BROADCAST | IFF_MULTICAST;
  dev_init_buffers (dev);

  ethernet_open (dev);

  /* Fetch hardware information */
  count = NET_STATUS_COUNT;
  err = device_get_status (edev->ether_port, NET_STATUS,
			   (dev_status_t) &netstat, &count);
  if (err)
    error (2, err, "%s: Cannot get device status", name);
  dev->mtu = netstat.max_packet_size - dev->hard_header_len;
  assert (netstat.header_format == HDR_ETHERNET);
  assert (netstat.header_size == ETH_HLEN);
  assert (netstat.address_size == ETH_ALEN);

  count = 2;
  assert (count * sizeof (int) >= ETH_ALEN);
  err = device_get_status (edev->ether_port, NET_ADDRESS, net_address, &count);
  if (err)
    error (2, err, "%s: Cannot get hardware Ethernet address", name);
  net_address[0] = ntohl (net_address[0]);
  net_address[1] = ntohl (net_address[1]);
  bcopy (net_address, dev->dev_addr, ETH_ALEN);

  /* That should be enough.  */

  /* This call adds the device to the `dev_base' chain,
     initializes its `ifindex' member (which matters!),
     and tells the protocol stacks about the device.  */
  err = - register_netdevice (dev);
  assert_perror (err);
}
