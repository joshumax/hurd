/* Loopback "device" for pfinet
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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

#include <linux/netdevice.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pfinet.h"

struct device loopback_dev;

int
loopback_xmit (struct sk_buff *skb, struct device *dev)
{
  int done;
  
  if (!skb || !dev)
    return 0;
  
  if (dev->tbusy)
    return 1;
  
  dev->tbusy;
  
  done = dev_rint (skb->data, skb->len, 0, dev);
  dev_kfree_skb (skb, FREE_WRITE);
  
  while (done != 1)
    done = dev_rint (0, 0, 0, dev);

  dev->tbusy = 0;
  return 0;
}
   

void
setup_loopback_device (char *name)
{
  int i;
  
  loopback_dev.name = name;
  for (i = 0; i < DEV_NUMBUFFS; i++)
    skb_queue_head_init (&loopback_dev.buffs[i]);
  
  loopback_dev.open = 0;
  loopback_dev.stop = 0;
  loopback_dev.hard_start_xmit = loopback_xmit;
  loopback_dev.hard_header = 0;
  loopback_dev.rebuild_header = 0;
  loopback_dev.type_trans = 0;
  loopback_dev.get_stats = 0;
  loopback_dev.set_multicast_list = 0;
  
  loopback_dev.type = 0;
  loopback_dev.addr_len = 0;
  loopback_dev.flags = IFF_LOOPBACK | IFF_BROADCAST;
  loopback_dev.family = AF_INET;

  loopback_dev.mtu = 2000;

  /* Defaults */
  loopback_dev.pa_addr = inet_addr ("127.0.0.1");
  loopback_dev.pa_brdaddr = inet_addr ("127.255.255.255");
  loopback_dev.pa_mask = inet_addr ("255.0.0.0");
  loopback_dev.pa_alen = sizeof (unsigned long);

  loopback_dev.next = dev_base;
  dev_base = &loopback_dev;

  /* Add the route */
  ip_rt_add (RTF_HOST, loopback_dev.pa_addr, 0xffffffff, 0, &loopback_dev,
	     loopback_dev.mtu, 0);
}



