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
struct device *ether_dev;

struct port_class *etherreadclass;
struct port_info *readpt;
mach_port_t readptname;

int
ethernet_demuxer (struct mach_msg_header_t *inp,
		  struct mach_msg_header_t *outp)
{
  struct net_rcv_msg *msg = (struct net_rcv_msg *) inp;
  struct packet_header *pkthdr = (struct packet_header *) msg->header;
  
  if (inp->msgh_id != 2999)
    return 0;
  
  if (inp->msgh_local_port != readptname)
    {
      if (inp->msgh_remote_port != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
      return 1;
    }
  
  if (ntohs (pkthdr->type) != HDR_ETHERNET)
    return 1;
  
  mutex_lock (&global_lock);
  skb = alloc_skb (msg->net_rcv_msg_packet_count, GFP_ATOMIC);
  skb->len = msg->net_rcv_msg_packet_count;
  sbx->dev = ether_dev;
  bcopy (msg->packet, skb->data, msg->net_rcv_msg_packet_count);
  mutex_unlock (&global_lock);
}

static short ether_filter[] = 
{
  NETF_PUSHLIT | NETF_NOP,
  1,
  NETF_PUSHZERO | NETF_OR,
};

static int ether_filter_len = 3;

void
start_ethernet (void)
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


