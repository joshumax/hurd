/* linux/net/inet/rarp.c
 *
 * Copyright (C) 1994 by Ross Martin
 * Based on linux/net/inet/arp.c, Copyright (C) 1994 by Florian La Roche
 *
 * $Id: rarp.c,v 1.25 1998/06/19 13:22:34 davem Exp $
 *
 * This module implements the Reverse Address Resolution Protocol 
 * (RARP, RFC 903), which is used to convert low level addresses such
 * as Ethernet addresses into high level addresses such as IP addresses.
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
 * Currently, only Ethernet address -> IP address is likely to work.
 * (Is RARP ever used for anything else?)
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Fixes
 *	Alan Cox	:	Rarp delete on device down needed as
 *				reported by Walter Wolfgang.
 *	Mike McLagan	:	Routing by source
 *
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <stdarg.h>
#include <linux/inet.h>
#include <linux/etherdevice.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/rarp.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <net/ax25.h>
#endif
#include <linux/proc_fs.h>
#include <linux/stat.h>

extern int (*rarp_ioctl_hook)(unsigned int,void*);

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

static int rarp_rcv(struct sk_buff *, struct device *, struct packet_type *);

static struct packet_type rarp_packet_type =
{
	0,  /* Should be: __constant_htons(ETH_P_RARP) - but this _doesn't_ come out constant! */
	0,                /* copy */
	rarp_rcv,
	NULL,
	NULL
};

static int initflag = 1;


/*
 *	Release the memory for this entry.
 */

static inline void rarp_release_entry(struct rarp_table *entry)
{
	kfree_s(entry, sizeof(struct rarp_table));
	MOD_DEC_USE_COUNT;
	return;
}

/*
 *	Delete a RARP mapping entry in the cache.
 */

static void rarp_destroy(unsigned long ip_addr)
{
	struct rarp_table *entry;
	struct rarp_table **pentry;
  
	start_bh_atomic();
	pentry = &rarp_tables;
	while ((entry = *pentry) != NULL)
	{
		if (entry->ip == ip_addr)
		{
			*pentry = entry->next;
			end_bh_atomic();
			rarp_release_entry(entry);
			return;
		}
		pentry = &entry->next;
	}
	end_bh_atomic();
}

/*
 *	Flush a device.
 */

static void rarp_destroy_dev(struct device *dev)
{
	struct rarp_table *entry;
	struct rarp_table **pentry;
  
	start_bh_atomic();
	pentry = &rarp_tables;
	while ((entry = *pentry) != NULL)
	{
		if (entry->dev == dev)
		{
			*pentry = entry->next;
			rarp_release_entry(entry);
		}
		else
			pentry = &entry->next;
	}
	end_bh_atomic();
}

static int rarp_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	if(event!=NETDEV_DOWN)
		return NOTIFY_DONE;
	rarp_destroy_dev((struct device *)ptr);
	return NOTIFY_DONE;
}

/*
 *	Called once when data first added to rarp cache with ioctl.
 */
 
static struct notifier_block rarp_dev_notifier={
	rarp_device_event,
	NULL,
	0
};

static int rarp_pkt_inited=0;
 
static void rarp_init_pkt (void)
{
	/* Register the packet type */
	rarp_packet_type.type=htons(ETH_P_RARP);
	dev_add_pack(&rarp_packet_type);
	register_netdevice_notifier(&rarp_dev_notifier);
	rarp_pkt_inited=1;
}

#ifdef MODULE

static void rarp_end_pkt(void)
{
	if(!rarp_pkt_inited)
		return;
	dev_remove_pack(&rarp_packet_type);
	unregister_netdevice_notifier(&rarp_dev_notifier);
	rarp_pkt_inited=0;
}

#endif

/*
 *	Receive an arp request by the device layer.  Maybe it should be 
 *	rewritten to use the incoming packet for the reply. The current 
 *	"overhead" time isn't that high...
 */

static int rarp_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
/*
 *	We shouldn't use this type conversion. Check later.
 */
	struct arphdr *rarp = (struct arphdr *) skb->data;
	unsigned char *rarp_ptr = skb_pull(skb,sizeof(struct arphdr));
	struct rarp_table *entry;
	struct in_device *in_dev = dev->ip_ptr;
	long sip,tip;
	unsigned char *sha,*tha;            /* s for "source", t for "target" */
	
/*
 *	If this test doesn't pass, it's not IP, or we should ignore it anyway
 */

	if (rarp->ar_hln != dev->addr_len || dev->type != ntohs(rarp->ar_hrd) 
		|| dev->flags&IFF_NOARP || !in_dev || !in_dev->ifa_list)
	{
		kfree_skb(skb);
		return 0;
	}

/*
 *	If it's not a RARP request, delete it.
 */
	if (rarp->ar_op != htons(ARPOP_RREQUEST))
	{
		kfree_skb(skb);
		return 0;
	}

/*
 *	For now we will only deal with IP addresses.
 */

	if (
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
		(rarp->ar_pro != htons(AX25_P_IP) && dev->type == ARPHRD_AX25) ||
#endif
		(rarp->ar_pro != htons(ETH_P_IP) && dev->type != ARPHRD_AX25)
		|| rarp->ar_pln != 4)
	{
		/*
		 *	This packet is not for us. Remove it. 
		 */
		kfree_skb(skb);
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
  
	for (entry = rarp_tables; entry != NULL; entry = entry->next)
		if (!memcmp(entry->ha, tha, rarp->ar_hln))
			break;
  
	if (entry != NULL)
	{
		sip=entry->ip;

		arp_send(ARPOP_RREPLY, ETH_P_RARP, sip, dev, in_dev->ifa_list->ifa_address, sha, 
			dev->dev_addr, sha);
	}

	kfree_skb(skb);
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
	struct device * dev;
	int err; 
  
	err = copy_from_user(&r, req, sizeof(r));
	if (err)
		return -EFAULT;

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
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
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
		printk(KERN_DEBUG "RARP: SETRARP: requested PA is 0.0.0.0 !\n");
		return -EINVAL;
	}
  
/*
 *	Is it reachable directly ?
 */
  
	err = ip_route_output(&rt, ip, 0, 1, 0);
	if (err)
		return err;
	if (rt->rt_flags&(RTCF_LOCAL|RTCF_BROADCAST|RTCF_MULTICAST|RTCF_DNAT)) {
		ip_rt_put(rt);
		return -EINVAL;
	}
	dev = rt->u.dst.dev;

/*
 *	Is there an existing entry for this address?  Find out...
 */
  
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
			return -ENOMEM;
		}
		if (initflag)
		{
			rarp_init_pkt();
			initflag=0;
		}

		/* Block interrupts until table modification is finished */

		cli();
		entry->next = rarp_tables;
		rarp_tables = entry;
	}
	cli();
	entry->ip = ip;
	entry->hlen = hlen;
	entry->htype = htype;
	memcpy(&entry->ha, &r.arp_ha.sa_data, hlen);
	entry->dev = dev;
	sti();

	/* Don't unlink if we have entries to serve. */
	MOD_INC_USE_COUNT;

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
	int err; 
	
/*
 *	We only understand about IP addresses...
 */
        
	err = copy_from_user(&r, req, sizeof(r));
	if (err)
		return -EFAULT; 

	if (r.arp_pa.sa_family != AF_INET)
		return -EPFNOSUPPORT;
  
/*
 *        Is there an existing entry for this address?
 */

	si = (struct sockaddr_in *) &r.arp_pa;
	ip = si->sin_addr.s_addr;

	for (entry = rarp_tables; entry != NULL; entry = entry->next)
		if (entry->ip == ip)
			break;

	if (entry == NULL)
	{
		return -ENXIO;
	}

/*
 *        We found it; copy into structure.
 */
        
	memcpy(r.arp_ha.sa_data, &entry->ha, entry->hlen);
	r.arp_ha.sa_family = entry->htype;
  
/*
 *        Copy the information back
 */
  
	return copy_to_user(req, &r, sizeof(r)) ? -EFAULT : 0;
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
			err = copy_from_user(&r, arg, sizeof(r));
			if (err)
				return -EFAULT; 
			if (r.arp_pa.sa_family != AF_INET)
				return -EPFNOSUPPORT;
			si = (struct sockaddr_in *) &r.arp_pa;
			rarp_destroy(si->sin_addr.s_addr);
			return 0;

		case SIOCGRARP:

			return rarp_req_get((struct arpreq *)arg);
		case SIOCSRARP:
			if (!suser())
				return -EPERM;
			return rarp_req_set((struct arpreq *)arg);
		default:
			return -EINVAL;
	}

	/*NOTREACHED*/
	return 0;
}

#ifdef CONFIG_PROC_FS
int rarp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	struct rarp_table *entry;
	char ipbuffer[20];
	unsigned long netip;
	if (initflag)
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
	}      

	*start = buffer+(offset-begin);	/* Start of wanted data */
	len   -= (offset-begin);	/* Start slop */
	if (len>length)
		len = length;		/* Ending slop */
	return len;
}

struct proc_dir_entry proc_net_rarp = {
	PROC_NET_RARP, 4, "rarp",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	rarp_get_info
};
#endif

__initfunc(void
rarp_init(void))
{
#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_rarp);
#endif
	rarp_ioctl_hook = rarp_ioctl;
}

#ifdef MODULE

int init_module(void)
{
	rarp_init();
	return 0;
}

void cleanup_module(void)
{
	struct rarp_table *rt, *rt_next;
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_RARP);
#endif
	rarp_ioctl_hook = NULL;
	cli();
	/* Destroy the RARP-table */
	rt = rarp_tables;
	rarp_tables = NULL;
	sti();
	/* ... and free it. */
	for ( ; rt != NULL; rt = rt_next) {
		rt_next = rt->next;
		rarp_release_entry(rt);
	}
	rarp_end_pkt();
}
#endif
