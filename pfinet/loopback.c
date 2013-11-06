/* Loopback "device" for pfinet
   Copyright (C) 1996,98,2000 Free Software Foundation, Inc.

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
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/socket.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/if_ether.h>	/* For the statistics structure. */
#include <linux/if_arp.h>	/* For ARPHRD_ETHER */

#define LOOPBACK_MTU	(vm_page_size - 172)

/*
 * The higher levels take care of making this non-reentrant (it's
 * called with bh's disabled).
 */
static int loopback_xmit(struct sk_buff *skb, struct device *dev)
{
	struct net_device_stats *stats = (struct net_device_stats *)dev->priv;

	/*
	 *	Take this out if the debug says its ok
	 */

	if (skb == NULL || dev == NULL)
		printk(KERN_DEBUG "loopback fed NULL data - splat\n");

	/*
	 *	Optimise so buffers with skb->free=1 are not copied but
	 *	instead are lobbed from tx queue to rx queue
	 */

	if(atomic_read(&skb->users) != 1)
	{
	  	struct sk_buff *skb2=skb;
	  	skb=skb_clone(skb, GFP_ATOMIC);		/* Clone the buffer */
	  	if(skb==NULL) {
			kfree_skb(skb2);
			return 0;
		}
	  	kfree_skb(skb2);
	}
	else
		skb_orphan(skb);

	skb->protocol=eth_type_trans(skb,dev);
	skb->dev=dev;
#ifndef LOOPBACK_MUST_CHECKSUM
	skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif

	/*
	 *	Calling netif_rx() requires locking net_bh_lock, which
	 *	has already been done since this function is called by
	 *	the net_bh worker thread.
	 */

	netif_rx(skb);

	stats->rx_bytes+=skb->len;
	stats->tx_bytes+=skb->len;
	stats->rx_packets++;
	stats->tx_packets++;

	return(0);
}

static struct net_device_stats *get_stats(struct device *dev)
{
	return (struct net_device_stats *)dev->priv;
}

static int loopback_open(struct device *dev)
{
	dev->flags|=IFF_LOOPBACK;
	return 0;
}

/* Initialize the rest of the LOOPBACK device. */
static int loopback_init(struct device *dev)
{
	dev->mtu		= LOOPBACK_MTU;
	dev->tbusy		= 0;
	dev->hard_start_xmit	= loopback_xmit;
	dev->hard_header	= eth_header;
	dev->hard_header_cache	= eth_header_cache;
	dev->header_cache_update= eth_header_cache_update;
	dev->hard_header_len	= ETH_HLEN;		/* 14			*/
	dev->addr_len		= ETH_ALEN;		/* 6			*/
	dev->tx_queue_len	= 0;
	dev->type		= ARPHRD_LOOPBACK;	/* 0x0001		*/
	dev->rebuild_header	= eth_rebuild_header;
	dev->open		= loopback_open;
	dev->flags		= IFF_LOOPBACK;
	dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);
	if (dev->priv == NULL)
			return -ENOMEM;
	memset(dev->priv, 0, sizeof(struct net_device_stats));
	dev->get_stats = get_stats;

	/*
	 *	Fill in the generic fields of the device structure.
	 */

	dev_init_buffers(dev);

	return(0);
}


struct device loopback_dev = { name: "lo", init: &loopback_init, };

/* It is important magic that this is the first thing on the list.  */
struct device *dev_base = &loopback_dev;
