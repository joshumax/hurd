/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Ethernet-type device handling.
 *
 * Version:	@(#)eth.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Florian  La Roche, <rzsfl@rz.uni-sb.de>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 * 
 * Fixes:
 *		Mr Linux	: Arp problems
 *		Alan Cox	: Generic queue tidyup (very tiny here)
 *		Alan Cox	: eth_header ntohs should be htons
 *		Alan Cox	: eth_rebuild_header missing an htons and
 *				  minor other things.
 *		Tegge		: Arp bug fixes. 
 *		Florian		: Removed many unnecessary functions, code cleanup
 *				  and changes for new arp and skbuff.
 *		Alan Cox	: Redid header building to reflect new format.
 *		Alan Cox	: ARP only when compiled with CONFIG_INET
 *		Greg Page	: 802.2 and SNAP stuff
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/config.h>

#include "arp.h"

void eth_setup(char *str, int *ints)
{
	struct device *d = dev_base;

	if (!str || !*str)
		return;
	while (d) 
	{
		if (!strcmp(str,d->name)) 
		{
			if (ints[0] > 0)
				d->irq=ints[1];
			if (ints[0] > 1)
				d->base_addr=ints[2];
			if (ints[0] > 2)
				d->mem_start=ints[3];
			if (ints[0] > 3)
				d->mem_end=ints[4];
			break;
		}
		d=d->next;
	}
}


/*
 *	 Create the Ethernet MAC header for an arbitrary protocol layer 
 *
 *	saddr=NULL	means use device source address
 *	daddr=NULL	means leave destination address (eg unresolved arp)
 */

int eth_header(unsigned char *buff, struct device *dev, unsigned short type,
	   void *daddr, void *saddr, unsigned len,
	   struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)buff;

	/* 
	 *	Set the protocol type. For a packet of type ETH_P_802_3 we put the length
	 *	in here instead. It is up to the 802.2 layer to carry protocol information.
	 */
	
	if(type!=ETH_P_802_3) 
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 *	Set the source hardware address. 
	 */
	 
	if(saddr)
		memcpy(eth->h_source,saddr,dev->addr_len);
	else
		memcpy(eth->h_source,dev->dev_addr,dev->addr_len);

	/*
	 *	Anyway, the loopback-device should never use this function... 
	 */

	if (dev->flags & IFF_LOOPBACK) 
	{
		memset(eth->h_dest, 0, dev->addr_len);
		return(dev->hard_header_len);
	}
	
	if(daddr)
	{
		memcpy(eth->h_dest,daddr,dev->addr_len);
		return dev->hard_header_len;
	}
	
	return -dev->hard_header_len;
}


/*
 *	Rebuild the Ethernet MAC header. This is called after an ARP
 *	(or in future other address resolution) has completed on this
 *	sk_buff. We now let ARP fill in the other fields.
 */
 
int eth_rebuild_header(void *buff, struct device *dev, unsigned long dst,
			struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)buff;

	/*
	 *	Only ARP/IP is currently supported
	 */
	 
	if(eth->h_proto != htons(ETH_P_IP)) 
	{
		printk("eth_rebuild_header: Don't know how to resolve type %d addresses?\n",(int)eth->h_proto);
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		return 0;
	}

	/*
	 *	Try and get ARP to resolve the header.
	 */
#ifdef CONFIG_INET	 
	return arp_find(eth->h_dest, dst, dev, dev->pa_addr, skb)? 1 : 0;
#else
	return 0;	
#endif	
}


/*
 *	Determine the packet's protocol ID. The rule here is that we 
 *	assume 802.3 if the type field is short enough to be a length.
 *	This is normal practice and works for any 'now in use' protocol.
 */
 
unsigned short eth_type_trans(struct sk_buff *skb, struct device *dev)
{
	struct ethhdr *eth = (struct ethhdr *) skb->data;
	unsigned char *rawp;
	
	if(*eth->h_dest&1)
	{
		if(memcmp(eth->h_dest,dev->broadcast, ETH_ALEN)==0)
			skb->pkt_type=PACKET_BROADCAST;
		else
			skb->pkt_type=PACKET_MULTICAST;
	}
	
	if(dev->flags&IFF_PROMISC)
	{
		if(memcmp(eth->h_dest,dev->dev_addr, ETH_ALEN))
			skb->pkt_type=PACKET_OTHERHOST;
	}
	
	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
		
	rawp = (unsigned char *)(eth + 1);
	
	if (*(unsigned short *)rawp == 0xFFFF)
		return htons(ETH_P_802_3);
		
	return htons(ETH_P_802_2);
}
