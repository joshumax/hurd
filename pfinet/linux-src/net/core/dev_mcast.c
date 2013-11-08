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
 *		Alan Cox	:	IFF_ALLMULTI support.
 *		Alan Cox	: 	New format set_multicast_list() calls.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/config.h> 
#include <asm/uaccess.h>
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
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>


/*
 *	Device multicast list maintenance. 
 *
 *	This is used both by IP and by the user level maintenance functions. 
 *	Unlike BSD we maintain a usage count on a given multicast address so 
 *	that a casual user application can add/delete multicasts used by 
 *	protocols without doing damage to the protocols when it deletes the
 *	entries. It also helps IP as it tracks overlapping maps.
 *
 *	Device mc lists are changed by bh at least if IPv6 is enabled,
 *	so that it must be bh protected.
 */

/*
 *	Update the multicast list into the physical NIC controller.
 */
 
void dev_mc_upload(struct device *dev)
{
	/* Don't do anything till we up the interface
	   [dev_open will call this function so the list will
	    stay sane] */
	    
	if(!(dev->flags&IFF_UP))
		return;

	/*
	 *	Devices with no set multicast don't get set 
	 */

	if(dev->set_multicast_list==NULL)
		return;

	start_bh_atomic();
	dev->set_multicast_list(dev);
	end_bh_atomic();
}
  
/*
 *	Delete a device level multicast
 */
 
int dev_mc_delete(struct device *dev, void *addr, int alen, int glbl)
{
	int err = 0;
	struct dev_mc_list *dmi, **dmip;

	start_bh_atomic();
	for (dmip=&dev->mc_list; (dmi=*dmip)!=NULL; dmip=&dmi->next) {
		/*
		 *	Find the entry we want to delete. The device could
		 *	have variable length entries so check these too.
		 */
		if (memcmp(dmi->dmi_addr,addr,dmi->dmi_addrlen)==0 && alen==dmi->dmi_addrlen) {
			if (glbl) {
				int old_glbl = dmi->dmi_gusers;
				dmi->dmi_gusers = 0;
				if (old_glbl == 0)
					break;
			}
			if(--dmi->dmi_users)
				goto done;

			/*
			 *	Last user. So delete the entry.
			 */
			*dmip = dmi->next;
			dev->mc_count--;
			kfree_s(dmi,sizeof(*dmi));
			/*
			 *	We have altered the list, so the card
			 *	loaded filter is now wrong. Fix it
			 */
			end_bh_atomic();
			dev_mc_upload(dev);
			return 0;
		}
	}
	err = -ENOENT;
done:
	end_bh_atomic();
	return err;
}

/*
 *	Add a device level multicast
 */
 
int dev_mc_add(struct device *dev, void *addr, int alen, int glbl)
{
	int err = 0;
	struct dev_mc_list *dmi, *dmi1;

	dmi1 = (struct dev_mc_list *)kmalloc(sizeof(*dmi), gfp_any());

	start_bh_atomic();
	for(dmi=dev->mc_list; dmi!=NULL; dmi=dmi->next) {
		if (memcmp(dmi->dmi_addr,addr,dmi->dmi_addrlen)==0 && dmi->dmi_addrlen==alen) {
			if (glbl) {
				int old_glbl = dmi->dmi_gusers;
				dmi->dmi_gusers = 1;
				if (old_glbl)
					goto done;
			}
			dmi->dmi_users++;
			goto done;
		}
	}

	if ((dmi=dmi1)==NULL)
		return -ENOMEM;
	memcpy(dmi->dmi_addr, addr, alen);
	dmi->dmi_addrlen=alen;
	dmi->next=dev->mc_list;
	dmi->dmi_users=1;
	dmi->dmi_gusers=glbl ? 1 : 0;
	dev->mc_list=dmi;
	dev->mc_count++;
	end_bh_atomic();
	dev_mc_upload(dev);
	return 0;

done:
	end_bh_atomic();
	if (dmi1)
		kfree(dmi1);
	return err;
}

/*
 *	Discard multicast list when a device is downed
 */

void dev_mc_discard(struct device *dev)
{
	start_bh_atomic();
	while (dev->mc_list!=NULL) {
		struct dev_mc_list *tmp=dev->mc_list;
		dev->mc_list=tmp->next;
		if (tmp->dmi_users > tmp->dmi_gusers)
			printk("dev_mc_discard: multicast leakage! dmi_users=%d\n", tmp->dmi_users);
		kfree_s(tmp,sizeof(*tmp));
	}
	dev->mc_count=0;
	end_bh_atomic();
}

#ifdef CONFIG_PROC_FS
static int dev_mc_read_proc(char *buffer, char **start, off_t offset,
			    int length, int *eof, void *data)
{
	off_t pos=0, begin=0;
	struct dev_mc_list *m;
	int len=0;
	struct device *dev;

	start_bh_atomic();

	for (dev = dev_base; dev; dev = dev->next) {
		for (m = dev->mc_list; m; m = m->next) {
			int i;

			len += sprintf(buffer+len,"%-4d %-15s %-5d %-5d ", dev->ifindex, dev->name,
				       m->dmi_users, m->dmi_gusers);

			for (i=0; i<m->dmi_addrlen; i++)
				len += sprintf(buffer+len, "%02x", m->dmi_addr[i]);

			len+=sprintf(buffer+len, "\n");

			pos=begin+len;
			if (pos < offset) {
				len=0;
				begin=pos;
			}
			if (pos > offset+length)
				goto done;
		}
	}
	*eof = 1;

done:
	end_bh_atomic();
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	if(len<0)
		len=0;
	return len;
}
#endif

__initfunc(void dev_mcast_init(void))
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *ent;

	ent = create_proc_entry("net/dev_mcast", 0, 0);
	ent->read_proc = dev_mc_read_proc;
#endif
}
