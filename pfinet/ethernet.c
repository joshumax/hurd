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


device_t ether_port;
struct device *ether_dev;

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
