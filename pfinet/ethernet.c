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

void
ethernet_main_loop (void)
{
  error_t err;
  vm_address_t packet;
  vm_size_t packetlen;
  struct sk_buff *skb;

  /* Listen to packets forever.  When one arrives,
     it's an "interrupt", so drop to interrupt layer, and call
     the generic code. */
  for (;;)
    {
      err = device_read (ether_port, 0, 0, vm_page_size, &packet,
			 &packetlen);
      if (err)
	{
	  perror ("Reading from ethernet");
	  continue;
	}
      
      begin_interrupt ();

      skb = alloc_skb (packetlen, GFP_ATOMIC);
      skb->len = packetlen;
      skb->dev = ether_dev;
      bcopy (packet, skb->data);
      netif_rx (skb);
    }
}

  



void
start_ethernet (void)
{
  cthread_detach (cthread_fork ((cthread_fn_t) ethernet_main_loop, 0));
}
