/*
 *	Linux NET3:	Multicast List maintenance. 
 *
 *	Authors:
 *		Tim Kordas <tjk@nostromo.eeap.cwru.edu> 
 *		Richard Underwood <richard@wuzz.demon.co.uk>
 *
 *	Stir fried together from the IP multicast and CAP patches above
 *		Alan Cox <Alan.Cox@linux.org>	
 *
 *	Fixes:
 *		Alan Cox	:	Update the device on a real delete
 *					rather than any time but...
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "ip.h"
#include "route.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"


/*
 *	Device multicast list maintenance. This knows about such little matters as promiscuous mode and
 *	converting from the list to the array the drivers use. At least until I fix the drivers up.
 *
 *	This is used both by IP and by the user level maintenance functions. Unlike BSD we maintain a usage count
 *	on a given multicast address so that a casual user application can add/delete multicasts used by protocols
 *	without doing damage to the protocols when it deletes the entries. It also helps IP as it tracks overlapping
 *	maps.
 */
 

/*
 *	Update the multicast list into the physical NIC controller.
 */
 
void dev_mc_upload(struct device *dev)
{
	struct dev_mc_list *dmi;
	char *data, *tmp;

	/* Don't do anything till we up the interface
	   [dev_open will call this function so the list will
	    stay sane] */
	    
	if(!(dev->flags&IFF_UP))
		return;
		
		
	/* Devices with no set multicast don't get set */
	if(dev->set_multicast_list==NULL)
		return;
	/* Promiscuous is promiscuous - so no filter needed */
	if(dev->flags&IFF_PROMISC)
	{
		dev->set_multicast_list(dev, -1, NULL);
		return;
	}
	
	if(dev->mc_count==0)
	{
		dev->set_multicast_list(dev,0,NULL);
		return;
	}
	
	data=kmalloc(dev->mc_count*dev->addr_len, GFP_KERNEL);
	if(data==NULL)
	{
		printk("Unable to get memory to set multicast list on %s\n",dev->name);
		return;
	}
	for(tmp = data, dmi=dev->mc_list;dmi!=NULL;dmi=dmi->next)
	{
		memcpy(tmp,dmi->dmi_addr, dmi->dmi_addrlen);
		tmp+=dev->addr_len;
	}
	dev->set_multicast_list(dev,dev->mc_count,data);
	kfree(data);
}
  
/*
 *	Delete a device level multicast
 */
 
void dev_mc_delete(struct device *dev, void *addr, int alen, int all)
{
	struct dev_mc_list **dmi;
	for(dmi=&dev->mc_list;*dmi!=NULL;dmi=&(*dmi)->next)
	{
		if(memcmp((*dmi)->dmi_addr,addr,(*dmi)->dmi_addrlen)==0 && alen==(*dmi)->dmi_addrlen)
		{
			struct dev_mc_list *tmp= *dmi;
			if(--(*dmi)->dmi_users && !all)
				return;
			*dmi=(*dmi)->next;
			dev->mc_count--;
			kfree_s(tmp,sizeof(*tmp));
			dev_mc_upload(dev);
			return;
		}
	}
}

/*
 *	Add a device level multicast
 */
 
void dev_mc_add(struct device *dev, void *addr, int alen, int newonly)
{
	struct dev_mc_list *dmi;
	for(dmi=dev->mc_list;dmi!=NULL;dmi=dmi->next)
	{
		if(memcmp(dmi->dmi_addr,addr,dmi->dmi_addrlen)==0 && dmi->dmi_addrlen==alen)
		{
			if(!newonly)
				dmi->dmi_users++;
			return;
		}
	}
	dmi=(struct dev_mc_list *)kmalloc(sizeof(*dmi),GFP_KERNEL);
	if(dmi==NULL)
		return;	/* GFP_KERNEL so can't happen anyway */
	memcpy(dmi->dmi_addr, addr, alen);
	dmi->dmi_addrlen=alen;
	dmi->next=dev->mc_list;
	dmi->dmi_users=1;
	dev->mc_list=dmi;
	dev->mc_count++;
	dev_mc_upload(dev);
}

/*
 *	Discard multicast list when a device is downed
 */

void dev_mc_discard(struct device *dev)
{
	while(dev->mc_list!=NULL)
	{
		struct dev_mc_list *tmp=dev->mc_list;
		dev->mc_list=dev->mc_list->next;
		kfree_s(tmp,sizeof(*tmp));
	}
	dev->mc_count=0;
}

