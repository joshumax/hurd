/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

#define ethername "el1"

device_t ether_port;

struct port_class *etherreadclass;
struct port_info *readpt;
mach_port_t readptname;

struct device ether_dev;

int
ethernet_demuxer (struct mach_msg_header_t *inp,
		  struct mach_msg_header_t *outp)
{
  struct net_rcv_msg *msg = (struct net_rcv_msg *) inp;
  struct packet_header *pkthdr = (struct packet_header *) msg->packet;
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
  
  if (ntohs (pkthdr->type) != HDR_ETHERNET)
    return 1;

  /* The total size is the size of the ethernet header (which had
     better equal sizeof (struct ethhdr) and the data portion of the
     frame.  So compute it. */
  if (msg->header_type.msgt_number != ETH_HLEN)
    return 1;
  
  datalen = ETH_HLEN 
    + msg->packet_type.msgt_number - sizeof (struct packet_header);

  mutex_lock (&global_lock);
  skb = alloc_skb (datalen, GFP_ATOMIC);
  skb->len = datalen;
  sbx->dev = ether_dev;

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

static short ether_filter[] = 
{
  NETF_PUSHLIT | NETF_NOP,
  1,
  NETF_PUSHZERO | NETF_OR,
};

static int ether_filter_len = 3;

/* Much of this is taken from Linux drivers/net/net_init.c: ether_setup */
void
setup_ethernet_device (void)
{
  int i;
  
  /* Interface buffers. */
  ether_dev->name = ethername;
  for (i = 0; i < DEV_NUMBUFFS; i++)
    skb_queue_head_init (&ether_dev->buffs[i]);

  /* Functions */
  ether_dev->open = ethernet_open;
  ether_dev->stop = ethernet_stop;
  ether_dev->hard_start_xmit = ethernet_xmit;
  ether_dev->hard_header = eth_header;
  ether_dev->rebuild_header = eth_rebuild_header;
  ether_dev->type_trans = eth_type_trans;
  ether_dev->get_stats = ethernet_get_stats;
  ether_dev->set_multicast_list = ethernet_set_multi;
  
  /* Some more fields */
  ether_dev->type = ARPHRD_ETHER;
  ether_dev->hard_header_len = sizeof (struct ethhdr);
  ether_dev->mtu = 1500;
  ether_dev->addr_len = 6;
  for (i = 0; i < 6; i++)
    ether_dev->broadcast[i] = 0xff;
  ether_dev->flags = IFF_BROADCAST | IFF_MULTICAST;
  ether_dev->family = AF_INET;	/* hmm. */
  ether_dev->pa_addr = ether_dev->pa_brdaddr = ether_dev->pa_mask = 0;
  ether_dev->pa_alen = sizeof (unsigned_long);

  /* That should be enough. */
}

struct enet_statistics retbuf;

/* Mach doesn't provide this.  DAMN. */
struct enet_statistics *
get_stats (struct device *dev)
{
  return &retbuf;
}




void
ethernet_open (void)
{
  etherreadclass = ports_create_class (0, 0);
  readpt = ports_allocate_port (pfinet_bucket, sizeof (struct port_info),
				etherreadclass);
  readptname = ports_get_right (readpt);
  mach_port_insert_right (mach_task_self (), readptname, readptname,
			  MACH_MSG_TYPE_MAKE_SEND);

  device_open (master_device, D_WRITE | D_READ, ethername, &ether_port);

  device_set_filter (ether_port, ports_get_right (readpt), 
		     MACH_MSG_TYPE_MAKE_SEND, NET_HI_PRI,
		     ether_filter, ether_filter_len);
}


