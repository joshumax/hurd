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

device_t ether_port;
struct device *ether_dev;

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
  

  


error_t
device_read_reply_inband (mach_port_t replypt,
			  error_t error_code,
			  vm_address_t data,
			  u_int datalen)
{
  mutex_lock (&global_lock);
  skb = alloc_skb (packetlen, GFP_ATOMIC);
  skb->len = packetlen;
  skb->dev = ether_dev;
  bcopy (packet, skb->data);
  netif_rx (skb);
  mutex_unlock (&global_lock);
  
  device_read_request (ether_port, ether_reply, 0, 0, vm_page_size);
}


void
start_ethernet (void)
{
  device_read_request (ether_port, ether_reply, 0, 0, vm_page_size);
}


