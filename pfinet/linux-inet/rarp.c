/* linux/net/inet/rarp.c
 *
 * Copyright (C) 1994 by Ross Martin
 * Based on linux/net/inet/arp.c, Copyright (C) 1994 by Florian La Roche
 *
 * This module implements the Reverse Address Resolution Protocol 
 * (RARP, RFC 903), which is used to convert low level addresses such
 * as ethernet addresses into high level addresses such as IP addresses.
 * The most common use of RARP is as a means for a diskless workstation 
 * to discover its IP address during a network boot.
 *
 **
 ***	WARNING:::::::::::::::::::::::::::::::::WARNING
 ****
 *****	SUN machines seem determined to boot solely from the person who
 ****	answered their RARP query. NEVER add a SUN to your RARP table
 ***	unless you have all the rest to boot the box from it. 
 **
 * 
 * Currently, only ethernet address -> IP address is likely to work.
 * (Is RARP ever used for anything else?)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <stdarg.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"
#include "rarp.h"
#ifdef CONFIG_AX25
#include "ax25.h"
#endif

#ifdef CONFIG_INET_RARP

/*
 *	This structure defines the RARP mapping cache. As long as we make 
 *	changes in this structure, we keep interrupts off.
 */

struct rarp_table
{
	struct rarp_table  *next;             /* Linked entry list           */
	unsigned long      ip;                /* ip address of entry         */
	unsigned char      ha[MAX_ADDR_LEN];  /* Hardware address            */
	unsigned char      hlen;              /* Length of hardware address  */
	unsigned char      htype;             /* Type of hardware in use     */
	struct device      *dev;              /* Device the entry is tied to */
};

struct rarp_table *rarp_tables = NULL;


static struct packet_type rarp_packet_type =
{
	0,  /* Should be: __constant_htons(ETH_P_RARP) - but this _doesn't_ come out constant! */
	0,                /* copy */
	rarp_rcv,
	NULL,
	NULL
};

static initflag = 1;

/*
 *	Called once when data first added to rarp cache with ioctl.
 */

static void rarp_init (void)
{
	/* Register the packet type */
	rarp_packet_type.type=htons(ETH_P_RARP);
	dev_add_pack(&rarp_packet_type);
}

/*
 *	Release the memory for this entry.
 */

static inline void rarp_release_entry(struct rarp_table *entry)
{
	kfree_s(entry, sizeof(struct rarp_table));
	return;
}

/*
 *	Delete a RARP mapping entry in the cache.
 */

static void rarp_destroy(unsigned long ip_addr)
{
	struct rarp_table *entry;
	struct rarp_table **pentry;
  
	cli();
	pentry = &rarp_tables;
	while ((entry = *pentry) != NULL)
	{
		if (entry->ip == ip_addr)
		{
			*pentry = entry->next;
			sti();
			rarp_release_entry(entry);
			return;
		}
		pentry = &entry->next;
	}
	sti();
}


/*
 *	Receive an arp request by the device layer.  Maybe it should be 
 *	rewritten to use the incoming packet for the reply. The current 
 *	"overhead" time isn't that high...
 */

int rarp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
/*
 *	We shouldn't use this type conversion. Check later.
 */
	struct arphdr *rarp = (struct arphdr *)skb->h.raw;
	unsigned char *rarp_ptr = (unsigned char *)(rarp+1);
	struct rarp_table *entry;
	long sip,tip;
	unsigned char *sha,*tha;            /* s for "source", t for "target" */
  
/*
 *	If this test doesn't pass, it's not IP, or we should ignore it anyway
 */

	if (rarp->ar_hln != dev->addr_len || dev->type != ntohs(rarp->ar_hrd) 
		|| dev->flags&IFF_NOARP)
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}

/*
 *	If it's not a RARP request, delete it.
 */
	if (rarp->ar_op != htons(ARPOP_RREQUEST))
	{
		kfree_skb(skb, FREE_READ);
		return 0;
	}

/*
 *	For now we will only deal with IP addresses.
 */

	if (
#ifdef CONFIG_AX25
		(rarp->ar_pro != htons(AX25_P_IP) && dev->type == ARPHRD_AX25) ||
#endif
		(rarp->ar_pro != htons(ETH_P_IP) && dev->type != ARPHRD_AX25)
		|| rarp->ar_pln != 4)
	{
	/*
	 *	This packet is not for us. Remove it. 
	 */
	kfree_skb(skb, FREE_READ);
	return 0;
}
  
/*
 *	Extract variable width fields
 */

	sha=rarp_ptr;
	rarp_ptr+=dev->addr_len;
	memcpy(&sip,rarp_ptr,4);
	rarp_ptr+=4;
	tha=rarp_ptr;
	rarp_ptr+=dev->addr_len;
	memcpy(&tip,rarp_ptr,4);

/*
 *	Process entry. Use tha for table lookup according to RFC903.
 */
  
	cli();
	for (entry = rarp_tables; entry != NULL; entry = entry->next)
		if (!memcmp(entry->ha, tha, rarp->ar_hln))
			break;
  
	if (entry != NULL)
	{
		sip=entry->ip;
		sti();

		arp_send(ARPOP_RREPLY, ETH_P_RARP, sip, dev, dev->pa_addr, sha, 
			dev->dev_addr);
	}
	else
		sti();

	kfree_skb(skb, FREE_READ);
	return 0;
}


/*
 *	Set (create) a RARP cache entry.
 */

static int rarp_req_set(struct arpreq *req)
{
	struct arpreq r;
	struct rarp_table *entry;
	struct sockaddr_in *si;
	int htype, hlen;
	unsigned long ip;
	struct rtable *rt;
  
	memcpy_fromfs(&r, req, sizeof(r));
  
	/*
	 *	We only understand about IP addresses... 
	 */

	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;
  
	switch (r.arp_ha.sa_family) 
	{
		case ARPHRD_ETHER:
			htype = ARPHRD_ETHER;
			hlen = ETH_ALEN;
			break;
#ifdef CONFIG_AX25
		case ARPHRD_AX25:
			htype = ARPHRD_AX25;
			hlen = 7;
		break;
#endif
		default:
			return -EPFNOSUPPORT;
	}

	si = (struct sockaddr_in *) &r.arp_pa;
	ip = si->sin_addr.s_addr;
	if (ip == 0)
	{
		printk("RARP: SETRARP: requested PA is 0.0.0.0 !\n");
		return -EINVAL;
	}
  
/*
 *	Is it reachable directly ?
 */
  
	rt = ip_rt_route(ip, NULL, NULL);
	if (rt == NULL)
		return -ENETUNREACH;

/*
 *	Is there an existing entry for this address?  Find out...
 */
  
	cli();
	for (entry = rarp_tables; entry != NULL; entry = entry->next)
		if (entry->ip == ip)
			break;
  
/*
 *	If no entry was found, create a new one.
 */

	if (entry == NULL)
	{
		entry = (struct rarp_table *) kmalloc(sizeof(struct rarp_table),
				    GFP_ATOMIC);
		if (entry == NULL)
		{
			sti();
			return -ENOMEM;
		}
		if(initflag)
		{
			rarp_init();
			initflag=0;
		}

		entry->next = rarp_tables;
		rarp_tables = entry;
	}

	entry->ip = ip;
	entry->hlen = hlen;
	entry->htype = htype;
	memcpy(&entry->ha, &r.arp_ha.sa_data, hlen);
	entry->dev = rt->rt_dev;

	sti();  

	return 0;
}


/*
 *        Get a RARP cache entry.
 */

static int rarp_req_get(struct arpreq *req)
{
	struct arpreq r;
	struct rarp_table *entry;
	struct sockaddr_in *si;
	unsigned long ip;

/*
 *	We only understand about IP addresses...
 */
        
	memcpy_fromfs(&r, req, sizeof(r));
  
	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;
  
/*
 *        Is there an existing entry for this address?
 */

	si = (struct sockaddr_in *) &r.arp_pa;
	ip = si->sin_addr.s_addr;

	cli();
	for (entry = rarp_tables; entry != NULL; entry = entry->next)
		if (entry->ip == ip)
			break;

	if (entry == NULL)
	{
		sti();
		return -ENXIO;
	}

/*
 *        We found it; copy into structure.
 */
        
	memcpy(r.arp_ha.sa_data, &entry->ha, entry->hlen);
	r.arp_ha.sa_family = entry->htype;
	sti();
  
/*
 *        Copy the information back
 */
  
	memcpy_tofs(req, &r, sizeof(r));
	return 0;
}


/*
 *	Handle a RARP layer I/O control request.
 */

int rarp_ioctl(unsigned int cmd, void *arg)
{
	struct arpreq r;
	struct sockaddr_in *si;
	int err;

	switch(cmd)
	{
		case SIOCDRARP:
			if (!suser())
				return -EPERM;
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq));
			if(err)
				return err;
			memcpy_fromfs(&r, arg, sizeof(r));
			if (r.arp_pa.sa_family != AF_INET)
				return -EPFNOSUPPORT;
			si = (struct sockaddr_in *) &r.arp_pa;
			rarp_destroy(si->sin_addr.s_addr);
			return 0;

		case SIOCGRARP:
			err = verify_area(VERIFY_WRITE, arg, sizeof(struct arpreq));
			if(err)
				return err;
			return rarp_req_get((struct arpreq *)arg);
		case SIOCSRARP:
			if (!suser())
				return -EPERM;
			err = verify_area(VERIFY_READ, arg, sizeof(struct arpreq));
			if(err)
				return err;
			return rarp_req_set((struct arpreq *)arg);
		default:
			return -EINVAL;
	}

	/*NOTREACHED*/
	return 0;
}

int rarp_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	struct rarp_table *entry;
	char ipbuffer[20];
	unsigned long netip;
	if(initflag)
	{
		size = sprintf(buffer,"RARP disabled until entries added to cache.\n");
		pos+=size;
		len+=size;
	}   
	else
	{
		size = sprintf(buffer,
			"IP address       HW type             HW address\n");
		pos+=size;
		len+=size;
      
		cli();
		for(entry=rarp_tables; entry!=NULL; entry=entry->next)
		{
			netip=htonl(entry->ip);          /* switch to network order */
			sprintf(ipbuffer,"%d.%d.%d.%d",
				(unsigned int)(netip>>24)&255,
				(unsigned int)(netip>>16)&255,
				(unsigned int)(netip>>8)&255,
				(unsigned int)(netip)&255);

			size = sprintf(buffer+len,
				"%-17s%-20s%02x:%02x:%02x:%02x:%02x:%02x\n",
				ipbuffer,
				"10Mbps Ethernet",
				(unsigned int)entry->ha[0],
				(unsigned int)entry->ha[1],
				(unsigned int)entry->ha[2],
				(unsigned int)entry->ha[3],
				(unsigned int)entry->ha[4],
			 	(unsigned int)entry->ha[5]);
	  
			len+=size;
			pos=begin+len;
	  
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				break;
		}
		sti();
	}      

	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		        /* Ending slop */
	return len;
}

#endif
