/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include <device/device.h>
#include <device/net_status.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <netinet/in.h>
#include <string.h>

#include "pfinet.h"

static char *ethername;

device_t ether_port;

struct port_class *etherreadclass;
struct port_info *readpt;
mach_port_t readptname;

struct device ether_dev;

struct enet_statistics retbuf;

static struct condition more_packets = CONDITION_INITIALIZER;

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
ethernet_set_multi (struct device *dev, int numaddrs, void *addrs)
{
  assert (numaddrs == 0);
}

static short ether_filter[] = 
{
  NETF_PUSHLIT | NETF_NOP,
  1,
  NETF_PUSHZERO | NETF_OR,
};

static int ether_filter_len = 3;
static struct port_bucket *etherport_bucket;

void
mark_bh (int arg)
{
  condition_broadcast (&more_packets);
}

any_t
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

  if (inp->msgh_id != NET_RCV_MSG_ID)
    return 0;
  
  if (inp->msgh_local_port != readptname)
    {
      if (inp->msgh_remote_port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
      return 1;
    }
  
  datalen = ETH_HLEN 
    + msg->packet_type.msgt_number - sizeof (struct packet_header);

  mutex_lock (&global_lock);
  skb = alloc_skb (datalen, GFP_ATOMIC);
  skb->len = datalen;
  skb->dev = &ether_dev;

  /* Copy the two parts of the frame into the buffer. */
  bcopy (msg->header, skb->data, ETH_HLEN);
  bcopy (msg->packet + sizeof (struct packet_header), 
	 skb->data + ETH_HLEN,
	 datalen - ETH_HLEN);

  /* Drop it on the queue. */
  netif_rx (skb);
  mutex_unlock (&global_lock);

  return 1;
}

any_t
input_work_thread (any_t arg)
{
  mutex_lock (&global_lock);
  for (;;)
    {
      condition_wait (&more_packets, &global_lock);
      net_bh (0);
    }
}

int
ethernet_open (struct device *dev)
{
  if (ether_port != MACH_PORT_NULL)
    return 0;
  
  etherreadclass = ports_create_class (0, 0);
  errno = ports_create_port (etherreadclass, etherport_bucket,
			     sizeof (struct port_info), &readpt);
  assert_perror (errno);
  readptname = ports_get_right (readpt);
  mach_port_insert_right (mach_task_self (), readptname, readptname,
			  MACH_MSG_TYPE_MAKE_SEND);

  mach_port_set_qlimit (mach_task_self (), readptname, MACH_PORT_QLIMIT_MAX);

  device_open (master_device, D_WRITE | D_READ, ethername, &ether_port);

  device_set_filter (ether_port, ports_get_right (readpt), 
		     MACH_MSG_TYPE_MAKE_SEND, 0,
		     ether_filter, ether_filter_len);
  cthread_detach (cthread_fork (ethernet_thread, 0));
  cthread_detach (cthread_fork (input_work_thread, 0));
  return 0;
}


/* Transmit an ethernet frame */
int
ethernet_xmit (struct sk_buff *skb, struct device *dev)
{
  u_int count;
  int err;
  
  err = device_write (ether_port, D_NOWAIT, 0, skb->data, skb->len, &count);
  assert (err == 0);
  assert (count == skb->len);
  dev_kfree_skb (skb, FREE_WRITE);
  return 0;
}

void
setup_ethernet_device (char *name)
{
  struct net_status netstat;
  u_int count;
  int net_address[2];
  int i;
  
  etherport_bucket = ports_create_bucket ();

  ethername = name;

  /* Interface buffers. */
  ether_dev.name = ethername;
  for (i = 0; i < DEV_NUMBUFFS; i++)
    skb_queue_head_init (&ether_dev.buffs[i]);

  /* Functions */
  ether_dev.open = ethernet_open;
  ether_dev.stop = ethernet_stop;
  ether_dev.hard_start_xmit = ethernet_xmit;
  ether_dev.hard_header = eth_header;
  ether_dev.rebuild_header = eth_rebuild_header;
  ether_dev.type_trans = eth_type_trans;
  ether_dev.get_stats = ethernet_get_stats;
  ether_dev.set_multicast_list = ethernet_set_multi;
  
  /* Some more fields */
  ether_dev.type = ARPHRD_ETHER;
  ether_dev.hard_header_len = sizeof (struct ethhdr);
  ether_dev.addr_len = ETH_ALEN;
  for (i = 0; i < 6; i++)
    ether_dev.broadcast[i] = 0xff;
  ether_dev.flags = IFF_BROADCAST | IFF_MULTICAST;
  ether_dev.family = AF_INET;	/* hmm. */
  ether_dev.pa_addr = ether_dev.pa_brdaddr = ether_dev.pa_mask = 0;
  ether_dev.pa_alen = sizeof (unsigned long);

  ethernet_open (&ether_dev);

  /* Fetch hardware information */
  count = NET_STATUS_COUNT;
  device_get_status (ether_port, NET_STATUS, (dev_status_t) &netstat, &count);
  ether_dev.mtu = netstat.max_packet_size;
  assert (netstat.header_format == HDR_ETHERNET);
  assert (netstat.header_size == ETH_HLEN);
  assert (netstat.address_size == ETH_ALEN);

  count = 2;
  assert (count * sizeof (int) >= ETH_ALEN);
  device_get_status (ether_port, NET_ADDRESS, net_address, &count);
  net_address[0] = ntohl (net_address[0]);
  net_address[1] = ntohl (net_address[1]);
  bcopy (net_address, ether_dev.dev_addr, ETH_ALEN);

  /* That should be enough. */

  ether_dev.next = dev_base;
  dev_base = &ether_dev;
}



