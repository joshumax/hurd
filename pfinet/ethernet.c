/*
   Copyright (C) 1995, 1996, 1998, 1999, 2000, 2002, 2007
     Free Software Foundation, Inc.

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

/* Do not include glue-include/linux/errno.h */
#define _HACK_ERRNO_H
#include "pfinet.h"

#include <device/device.h>
#include <device/net_status.h>
#include <netinet/in.h>
#include <string.h>
#include <error.h>
#include <fcntl.h>

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

static struct bpf_insn bpf_ether_filter[] =
{
    {NETF_IN|NETF_BPF, 0, 0, 0},		/* Header. */
    {BPF_LD|BPF_H|BPF_ABS, 0, 0, 12},		/* Load Ethernet type */
    {BPF_JMP|BPF_JEQ|BPF_K, 2, 0, 0x0806},	/* Accept ARP */
    {BPF_JMP|BPF_JEQ|BPF_K, 1, 0, 0x0800},	/* Accept IPv4 */
    {BPF_JMP|BPF_JEQ|BPF_K, 0, 1, 0x86DD},	/* Accept IPv6 */
    {BPF_RET|BPF_K, 0, 0, 1500},		/* And return 1500 bytes */
    {BPF_RET|BPF_K, 0, 0, 0},			/* Or discard it all */
};
static int bpf_ether_filter_len = sizeof (bpf_ether_filter) / sizeof (short);

static struct port_bucket *etherport_bucket;


static void *
ethernet_thread (void *arg)
{
  ports_manage_port_operations_one_thread (etherport_bucket,
					   ethernet_demuxer,
					   0);
  return NULL;
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
  mach_port_t local_port;

  if (inp->msgh_id != NET_RCV_MSG_ID)
    return 0;

  if (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) ==
      MACH_MSG_TYPE_PROTECTED_PAYLOAD)
    {
      struct port_info *pi = ports_lookup_payload (NULL,
						   inp->msgh_protected_payload,
						   NULL);
      if (pi)
	{
	  local_port = pi->port_right;
	  ports_port_deref (pi);
	}
      else
	local_port = MACH_PORT_NULL;
    }
  else
    local_port = inp->msgh_local_port;

  for (edev = ether_dev; edev; edev = edev->next)
    if (local_port == edev->readptname)
      dev = &edev->dev;

  if (! dev)
    {
      if (inp->msgh_remote_port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
      return 1;
    }

  datalen = ETH_HLEN
    + msg->packet_type.msgt_number - sizeof (struct packet_header);

  pthread_mutex_lock (&net_bh_lock);
  skb = alloc_skb (datalen, GFP_ATOMIC);
  skb_put (skb, datalen);
  skb->dev = dev;

  /* Copy the two parts of the frame into the buffer. */
  memcpy (skb->data, msg->header, ETH_HLEN);
  memcpy (skb->data + ETH_HLEN,
	  msg->packet + sizeof (struct packet_header),
	  datalen - ETH_HLEN);

  /* Drop it on the queue. */
  skb->protocol = eth_type_trans (skb, dev);
  netif_rx (skb);
  pthread_mutex_unlock (&net_bh_lock);

  return 1;
}


void
ethernet_initialize (void)
{
  pthread_t thread;
  error_t err;
  etherport_bucket = ports_create_bucket ();
  etherreadclass = ports_create_class (0, 0);

  err = pthread_create (&thread, NULL, ethernet_thread, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
}

int
ethernet_open (struct device *dev)
{
  error_t err;
  device_t master_device;
  struct ether_device *edev = (struct ether_device *) dev->priv;

  assert_backtrace (edev->ether_port == MACH_PORT_NULL);

  err = ports_create_port (etherreadclass, etherport_bucket,
			   sizeof (struct port_info), &edev->readpt);
  assert_perror_backtrace (err);
  edev->readptname = ports_get_right (edev->readpt);
  mach_port_insert_right (mach_task_self (), edev->readptname, edev->readptname,
			  MACH_MSG_TYPE_MAKE_SEND);

  mach_port_set_qlimit (mach_task_self (), edev->readptname, MACH_PORT_QLIMIT_MAX);

  master_device = file_name_lookup (dev->name, O_READ | O_WRITE, 0);
  if (master_device != MACH_PORT_NULL)
    {
      /* The device name here is the path of a device file.  */
      err = device_open (master_device, D_WRITE | D_READ, "eth", &edev->ether_port);
      mach_port_deallocate (mach_task_self (), master_device);
      if (err)
	error (2, err, "device_open on %s", dev->name);

      err = device_set_filter (edev->ether_port, ports_get_right (edev->readpt),
			       MACH_MSG_TYPE_MAKE_SEND, 0,
			       bpf_ether_filter, bpf_ether_filter_len);
      if (err)
	error (2, err, "device_set_filter on %s", dev->name);
    }
  else
    {
      /* No, perhaps a Mach device?  */
      int file_errno = errno;
      err = get_privileged_ports (0, &master_device);
      if (err)
	{
	  error (0, file_errno, "file_name_lookup %s", dev->name);
	  error (2, err, "and cannot get device master port");
	}
      err = device_open (master_device, D_WRITE | D_READ, dev->name, &edev->ether_port);
      mach_port_deallocate (mach_task_self (), master_device);
      if (err)
	{
	  error (0, file_errno, "file_name_lookup %s", dev->name);
	  error (2, err, "device_open(%s)", dev->name);
	}

      err = device_set_filter (edev->ether_port, ports_get_right (edev->readpt),
			       MACH_MSG_TYPE_MAKE_SEND, 0,
			       ether_filter, ether_filter_len);
      if (err)
	error (2, err, "device_set_filter on %s", dev->name);
    }

  return 0;
}

int
ethernet_close (struct device *dev)
{
  struct ether_device *edev = (struct ether_device *) dev->priv;

  mach_port_deallocate (mach_task_self (), edev->readptname);
  edev->readptname = MACH_PORT_NULL;
  ports_destroy_right (edev->readpt);
  edev->readpt = NULL;
  device_close (edev->ether_port);
  mach_port_deallocate (mach_task_self (), edev->ether_port);
  edev->ether_port = MACH_PORT_NULL;
}

/* Transmit an ethernet frame */
int
ethernet_xmit (struct sk_buff *skb, struct device *dev)
{
  error_t err;
  struct ether_device *edev = (struct ether_device *) dev->priv;
  u_int count;
  u_int tried = 0;

  do
    {
      tried++;
      err = device_write (edev->ether_port, D_NOWAIT, 0, skb->data, skb->len, &count);
      if (err == EMACH_SEND_INVALID_DEST || err == EMIG_SERVER_DIED)
	{
	  /* Device probably just died, try to reopen it.  */

	  if (tried == 2)
	    /* Too many tries, abort */
	    break;

	  ethernet_close (dev);
	  ethernet_open (dev);
	}
      else
	{
	  assert_perror_backtrace (err);
	  assert_backtrace (count == skb->len);
	}
    }
  while (err);

  dev_kfree_skb (skb);
  return 0;
}

/* Set device flags (e.g. promiscuous) */
static int
ethernet_change_flags (struct device *dev, short flags)
{
  error_t err = 0;
#ifdef NET_FLAGS
  int status = flags;
  struct ether_device *edev = (struct ether_device *) dev->priv;
  err = device_set_status (edev->ether_port, NET_FLAGS, &status, 1);
  if (err == D_INVALID_OPERATION)
    /* Not supported, ignore.  */
    err = 0;
#endif
  return err;
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

  /* FIXME: Receive all multicast to fix IPv6, until we implement
     ethernet_set_multi.  */
  dev->flags |= IFF_ALLMULTI;

  dev->change_flags = ethernet_change_flags;

  dev_init_buffers (dev);

  ethernet_open (dev);

  /* Fetch hardware information */
  count = NET_STATUS_COUNT;
  err = device_get_status (edev->ether_port, NET_STATUS,
			   (dev_status_t) &netstat, &count);
  if (err)
    error (2, err, "%s: Cannot get device status", name);
  dev->mtu = netstat.max_packet_size - dev->hard_header_len;
  assert_backtrace (netstat.header_format == HDR_ETHERNET);
  assert_backtrace (netstat.header_size == ETH_HLEN);
  assert_backtrace (netstat.address_size == ETH_ALEN);

  count = 2;
  assert_backtrace (count * sizeof (int) >= ETH_ALEN);
  err = device_get_status (edev->ether_port, NET_ADDRESS, net_address, &count);
  if (err)
    error (2, err, "%s: Cannot get hardware Ethernet address", name);
  net_address[0] = ntohl (net_address[0]);
  net_address[1] = ntohl (net_address[1]);
  memcpy (dev->dev_addr, net_address, ETH_ALEN);

  /* That should be enough.  */

  /* This call adds the device to the `dev_base' chain,
     initializes its `ifindex' member (which matters!),
     and tells the protocol stacks about the device.  */
  err = - register_netdevice (dev);
  assert_perror_backtrace (err);
}
