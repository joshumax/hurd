/*
 *	IP multicast routing support for mrouted 3.6/3.8
 *
 *		(c) 1995 Alan Cox, <alan@redhat.com>
 *	  Linux Consultancy and Custom Driver Development
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Version: $Id: ipmr.c,v 1.40.2.2 1999/06/20 21:27:44 davem Exp $
 *
 *	Fixes:
 *	Michael Chastain	:	Incorrect size of copying.
 *	Alan Cox		:	Added the cache manager code
 *	Alan Cox		:	Fixed the clone/copy bug and device race.
 *	Mike McLagan		:	Routing by source
 *	Malcolm Beattie		:	Buffer handling fixes.
 *	Alexey Kuznetsov	:	Double buffer free and other fixes.
 *	SVR Anand		:	Fixed several multicast bugs and problems.
 *	Alexey Kuznetsov	:	Status, optimisations and more.
 *	Brad Parker		:	Better behaviour on mrouted upcall
 *					overflow.
 *      Carlos Picoto           :       PIMv1 Support
 *	Pavlin Ivanov Radoslavov:	PIMv2 Registers must checksum only PIM header
 *					Relax this requrement to work with older peers.
 *
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/proc_fs.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/notifier.h>
#include <linux/if_arp.h>
#include <linux/ip_fw.h>
#include <linux/firewall.h>
#include <net/ipip.h>
#include <net/checksum.h>

#if defined(CONFIG_IP_PIMSM_V1) || defined(CONFIG_IP_PIMSM_V2)
#define CONFIG_IP_PIMSM	1
#endif

/*
 *	Multicast router control variables
 */

static struct vif_device vif_table[MAXVIFS];		/* Devices 		*/
static unsigned long vifc_map;				/* Active device map	*/
static int maxvif;
int mroute_do_assert = 0;				/* Set in PIM assert	*/
int mroute_do_pim = 0;
static struct mfc_cache *mfc_cache_array[MFC_LINES];	/* Forwarding cache	*/
int cache_resolve_queue_len = 0;			/* Size of unresolved	*/

static int ip_mr_forward(struct sk_buff *skb, struct mfc_cache *cache, int local);
static int ipmr_cache_report(struct sk_buff *pkt, vifi_t vifi, int assert);
static int ipmr_fill_mroute(struct sk_buff *skb, struct mfc_cache *c, struct rtmsg *rtm);

extern struct inet_protocol pim_protocol;

static
struct device *ipmr_new_tunnel(struct vifctl *v)
{
	struct device  *dev = NULL;

	rtnl_lock();
	dev = dev_get("tunl0");

	if (dev) {
		int err;
		struct ifreq ifr;
		mm_segment_t	oldfs;
		struct ip_tunnel_parm p;
		struct in_device  *in_dev;

		memset(&p, 0, sizeof(p));
		p.iph.daddr = v->vifc_rmt_addr.s_addr;
		p.iph.saddr = v->vifc_lcl_addr.s_addr;
		p.iph.version = 4;
		p.iph.ihl = 5;
		p.iph.protocol = IPPROTO_IPIP;
		sprintf(p.name, "dvmrp%d", v->vifc_vifi);
		ifr.ifr_ifru.ifru_data = (void*)&p;

		oldfs = get_fs(); set_fs(KERNEL_DS);
		err = dev->do_ioctl(dev, &ifr, SIOCADDTUNNEL);
		set_fs(oldfs);

		if (err == 0 && (dev = dev_get(p.name)) != NULL) {
			dev->flags |= IFF_MULTICAST;

			in_dev = dev->ip_ptr;
			if (in_dev == NULL && (in_dev = inetdev_init(dev)) == NULL)
				goto failure;
			in_dev->cnf.rp_filter = 0;

			if (dev_open(dev))
				goto failure;
		}
	}
	rtnl_unlock();
	return dev;

failure:
	unregister_netdevice(dev);
	rtnl_unlock();
	return NULL;
}

#ifdef CONFIG_IP_PIMSM

static int reg_vif_num = -1;
static struct device * reg_dev;

static int reg_vif_xmit(struct sk_buff *skb, struct device *dev)
{
	((struct net_device_stats*)dev->priv)->tx_bytes += skb->len;
	((struct net_device_stats*)dev->priv)->tx_packets++;
	ipmr_cache_report(skb, reg_vif_num, IGMPMSG_WHOLEPKT);
	kfree_skb(skb);
	return 0;
}

static struct net_device_stats *reg_vif_get_stats(struct device *dev)
{
	return (struct net_device_stats*)dev->priv;
}

static
struct device *ipmr_reg_vif(struct vifctl *v)
{
	struct device  *dev;
	struct in_device *in_dev;
	int size;

	size = sizeof(*dev) + IFNAMSIZ + sizeof(struct net_device_stats);
	dev = kmalloc(size, GFP_KERNEL);
	if (!dev)
		return NULL;

	memset(dev, 0, size);

	dev->priv = dev + 1;
	dev->name = dev->priv + sizeof(struct net_device_stats);

	strcpy(dev->name, "pimreg");

	dev->type		= ARPHRD_PIMREG;
	dev->mtu		= 1500 - sizeof(struct iphdr) - 8;
	dev->flags		= IFF_NOARP;
	dev->hard_start_xmit	= reg_vif_xmit;
	dev->get_stats		= reg_vif_get_stats;

	rtnl_lock();

	if (register_netdevice(dev)) {
		rtnl_unlock();
		kfree(dev);
		return NULL;
	}
	dev->iflink = 0;

	if ((in_dev = inetdev_init(dev)) == NULL)
		goto failure;

	in_dev->cnf.rp_filter = 0;

	if (dev_open(dev))
		goto failure;

	rtnl_unlock();
	reg_dev = dev;
	return dev;

failure:
	unregister_netdevice(dev);
	rtnl_unlock();
	kfree(dev);
	return NULL;
}
#endif

/*
 *	Delete a VIF entry
 */
 
static int vif_delete(int vifi)
{
	struct vif_device *v;
	struct device *dev;
	struct in_device *in_dev;
	
	if (vifi < 0 || vifi >= maxvif || !(vifc_map&(1<<vifi)))
		return -EADDRNOTAVAIL;

	v = &vif_table[vifi];

	dev = v->dev;
	v->dev = NULL;
	vifc_map &= ~(1<<vifi);

	if ((in_dev = dev->ip_ptr) != NULL)
		in_dev->cnf.mc_forwarding = 0;

	dev_set_allmulti(dev, -1);
	ip_rt_multicast_event(in_dev);

	if (v->flags&(VIFF_TUNNEL|VIFF_REGISTER)) {
#ifdef CONFIG_IP_PIMSM
		if (vifi == reg_vif_num) {
			reg_vif_num = -1;
			reg_dev = NULL;
		}
#endif
		unregister_netdevice(dev);
		if (v->flags&VIFF_REGISTER)
			kfree(dev);
	}

	if (vifi+1 == maxvif) {
		int tmp;
		for (tmp=vifi-1; tmp>=0; tmp--) {
			if (vifc_map&(1<<tmp))
				break;
		}
		maxvif = tmp+1;
	}
	return 0;
}

static void ipmr_update_threshoulds(struct mfc_cache *cache, unsigned char *ttls)
{
	int vifi;

	start_bh_atomic();

	cache->mfc_minvif = MAXVIFS;
	cache->mfc_maxvif = 0;
	memset(cache->mfc_ttls, 255, MAXVIFS);

	for (vifi=0; vifi<maxvif; vifi++) {
		if (vifc_map&(1<<vifi) && ttls[vifi] && ttls[vifi] < 255) {
			cache->mfc_ttls[vifi] = ttls[vifi];
			if (cache->mfc_minvif > vifi)
				cache->mfc_minvif = vifi;
			if (cache->mfc_maxvif <= vifi)
				cache->mfc_maxvif = vifi + 1;
		}
	}
	end_bh_atomic();
}

/*
 *	Delete a multicast route cache entry
 */
 
static void ipmr_cache_delete(struct mfc_cache *cache)
{
	struct sk_buff *skb;
	int line;
	struct mfc_cache **cp;
	
	/*
	 *	Find the right cache line
	 */

	line=MFC_HASH(cache->mfc_mcastgrp,cache->mfc_origin);
	cp=&(mfc_cache_array[line]);

	if(cache->mfc_flags&MFC_QUEUED)
		del_timer(&cache->mfc_timer);
	
	/*
	 *	Unlink the buffer
	 */

	while(*cp!=NULL)
	{
		if(*cp==cache)
		{
			*cp=cache->next;
			break;
		}
		cp=&((*cp)->next);
	}

	/*
	 *	Free the buffer. If it is a pending resolution
	 *	clean up the other resources.
	 */

	if(cache->mfc_flags&MFC_QUEUED)
	{
		cache_resolve_queue_len--;
		while((skb=skb_dequeue(&cache->mfc_unresolved))) {
#ifdef CONFIG_RTNETLINK
			if (skb->nh.iph->version == 0) {
				struct nlmsghdr *nlh = (struct nlmsghdr *)skb_pull(skb, sizeof(struct iphdr));
				nlh->nlmsg_type = NLMSG_ERROR;
				nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
				skb_trim(skb, nlh->nlmsg_len);
				((struct nlmsgerr*)NLMSG_DATA(nlh))->error = -ETIMEDOUT;
				netlink_unicast(rtnl, skb, NETLINK_CB(skb).dst_pid, MSG_DONTWAIT);
			} else
#endif
			kfree_skb(skb);
		}
	}
	kfree_s(cache,sizeof(*cache));
}

/*
 *	Cache expiry timer
 */	
 
static void ipmr_cache_timer(unsigned long data)
{
	struct mfc_cache *cache=(struct mfc_cache *)data;
	ipmr_cache_delete(cache);
}

/*
 *	Insert a multicast cache entry
 */

static void ipmr_cache_insert(struct mfc_cache *c)
{
	int line=MFC_HASH(c->mfc_mcastgrp,c->mfc_origin);
	c->next=mfc_cache_array[line];
	mfc_cache_array[line]=c;
}
 
/*
 *	Find a multicast cache entry
 */
 
struct mfc_cache *ipmr_cache_find(__u32 origin, __u32 mcastgrp)
{
	int line=MFC_HASH(mcastgrp,origin);
	struct mfc_cache *cache;

	cache=mfc_cache_array[line];
	while(cache!=NULL)
	{
		if(cache->mfc_origin==origin && cache->mfc_mcastgrp==mcastgrp)
			return cache;
		cache=cache->next;
	}
	return NULL;
}

/*
 *	Allocate a multicast cache entry
 */
 
static struct mfc_cache *ipmr_cache_alloc(int priority)
{
	struct mfc_cache *c=(struct mfc_cache *)kmalloc(sizeof(struct mfc_cache), priority);
	if(c==NULL)
		return NULL;
	memset(c, 0, sizeof(*c));
	skb_queue_head_init(&c->mfc_unresolved);
	init_timer(&c->mfc_timer);
	c->mfc_timer.data=(long)c;
	c->mfc_timer.function=ipmr_cache_timer;
	c->mfc_minvif = MAXVIFS;
	return c;
}
 
/*
 *	A cache entry has gone into a resolved state from queued
 */
 
static void ipmr_cache_resolve(struct mfc_cache *cache)
{
	struct sk_buff *skb;

	start_bh_atomic();

	/*
	 *	Kill the queue entry timer.
	 */

	del_timer(&cache->mfc_timer);

	if (cache->mfc_flags&MFC_QUEUED) {
		cache->mfc_flags&=~MFC_QUEUED;
		cache_resolve_queue_len--;
	}

	end_bh_atomic();

	/*
	 *	Play the pending entries through our router
	 */
	while((skb=skb_dequeue(&cache->mfc_unresolved))) {
#ifdef CONFIG_RTNETLINK
		if (skb->nh.iph->version == 0) {
			int err;
			struct nlmsghdr *nlh = (struct nlmsghdr *)skb_pull(skb, sizeof(struct iphdr));

			if (ipmr_fill_mroute(skb, cache, NLMSG_DATA(nlh)) > 0) {
				nlh->nlmsg_len = skb->tail - (u8*)nlh;
			} else {
				nlh->nlmsg_type = NLMSG_ERROR;
				nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct nlmsgerr));
				skb_trim(skb, nlh->nlmsg_len);
				((struct nlmsgerr*)NLMSG_DATA(nlh))->error = -EMSGSIZE;
			}
			err = netlink_unicast(rtnl, skb, NETLINK_CB(skb).dst_pid, MSG_DONTWAIT);
		} else
#endif
			ip_mr_forward(skb, cache, 0);
	}
}

/*
 *	Bounce a cache query up to mrouted. We could use netlink for this but mrouted
 *	expects the following bizarre scheme..
 */
 
static int ipmr_cache_report(struct sk_buff *pkt, vifi_t vifi, int assert)
{
	struct sk_buff *skb;
	int ihl = pkt->nh.iph->ihl<<2;
	struct igmphdr *igmp;
	struct igmpmsg *msg;
	int ret;

	if (mroute_socket==NULL)
		return -EINVAL;

#ifdef CONFIG_IP_PIMSM
	if (assert == IGMPMSG_WHOLEPKT)
		skb = skb_realloc_headroom(pkt, sizeof(struct iphdr));
	else
#endif
		skb = alloc_skb(128, GFP_ATOMIC);

	if(!skb)
		return -ENOBUFS;

#ifdef CONFIG_IP_PIMSM
	if (assert == IGMPMSG_WHOLEPKT) {
		/* Ugly, but we have no choice with this interface.
		   Duplicate old header, fix ihl, length etc.
		   And all this only to mangle msg->im_msgtype and
		   to set msg->im_mbz to "mbz" :-)
		 */
		msg = (struct igmpmsg*)skb_push(skb, sizeof(struct iphdr));
		skb->nh.raw = skb->h.raw = (u8*)msg;
		memcpy(msg, pkt->nh.raw, sizeof(struct iphdr));
		msg->im_msgtype = IGMPMSG_WHOLEPKT;
		msg->im_mbz = 0;
 		msg->im_vif = reg_vif_num;
		skb->nh.iph->ihl = sizeof(struct iphdr) >> 2;
		skb->nh.iph->tot_len = htons(ntohs(pkt->nh.iph->tot_len) + sizeof(struct iphdr));
	} else 
#endif
	{	
		
	/*
	 *	Copy the IP header
	 */

	skb->nh.iph = (struct iphdr *)skb_put(skb, ihl);
	memcpy(skb->data,pkt->data,ihl);
	skb->nh.iph->protocol = 0;			/* Flag to the kernel this is a route add */
	msg = (struct igmpmsg*)skb->nh.iph;
	msg->im_vif = vifi;
	skb->dst = dst_clone(pkt->dst);

	/*
	 *	Add our header
	 */

	igmp=(struct igmphdr *)skb_put(skb,sizeof(struct igmphdr));
	igmp->type	=
	msg->im_msgtype = assert;
	igmp->code 	=	0;
	skb->nh.iph->tot_len=htons(skb->len);			/* Fix the length */
	skb->h.raw = skb->nh.raw;
        }
	
	/*
	 *	Deliver to mrouted
	 */
	if ((ret=sock_queue_rcv_skb(mroute_socket,skb))<0) {
		if (net_ratelimit())
			printk(KERN_WARNING "mroute: pending queue full, dropping entries.\n");
		kfree_skb(skb);
	}

	return ret;
}

/*
 *	Queue a packet for resolution
 */
 
static int ipmr_cache_unresolved(struct mfc_cache *cache, vifi_t vifi, struct sk_buff *skb)
{
	if(cache==NULL)
	{	
		/*
		 *	Create a new entry if allowable
		 */
		if(cache_resolve_queue_len>=10 || (cache=ipmr_cache_alloc(GFP_ATOMIC))==NULL)
		{
			kfree_skb(skb);
			return -ENOBUFS;
		}
		/*
		 *	Fill in the new cache entry
		 */
		cache->mfc_parent=ALL_VIFS;
		cache->mfc_origin=skb->nh.iph->saddr;
		cache->mfc_mcastgrp=skb->nh.iph->daddr;
		cache->mfc_flags=MFC_QUEUED;
		/*
		 *	Link to the unresolved list
		 */
		ipmr_cache_insert(cache);
		cache_resolve_queue_len++;
		/*
		 *	Fire off the expiry timer
		 */
		cache->mfc_timer.expires=jiffies+10*HZ;
		add_timer(&cache->mfc_timer);
		/*
		 *	Reflect first query at mrouted.
		 */
		if(mroute_socket)
		{
			/* If the report failed throw the cache entry 
			   out - Brad Parker

			   OK, OK, Brad. Only do not forget to free skb
			   and return :-) --ANK
			 */
			if (ipmr_cache_report(skb, vifi, IGMPMSG_NOCACHE)<0) {
				ipmr_cache_delete(cache);
				kfree_skb(skb);
				return -ENOBUFS;
			}
		}
	}
	/*
	 *	See if we can append the packet
	 */
	if(cache->mfc_queuelen>3)
	{
		kfree_skb(skb);
		return -ENOBUFS;
	}
	cache->mfc_queuelen++;
	skb_queue_tail(&cache->mfc_unresolved,skb);
	return 0;
}

/*
 *	MFC cache manipulation by user space mroute daemon
 */
 
int ipmr_mfc_modify(int action, struct mfcctl *mfc)
{
	struct mfc_cache *cache;

	if(!MULTICAST(mfc->mfcc_mcastgrp.s_addr))
		return -EINVAL;
	/*
	 *	Find the cache line
	 */
	
	start_bh_atomic();

	cache=ipmr_cache_find(mfc->mfcc_origin.s_addr,mfc->mfcc_mcastgrp.s_addr);
	
	/*
	 *	Delete an entry
	 */
	if(action==MRT_DEL_MFC)
	{
		if(cache)
		{
			ipmr_cache_delete(cache);
			end_bh_atomic();
			return 0;
		}
		end_bh_atomic();
		return -ENOENT;
	}
	if(cache)
	{

		/*
		 *	Update the cache, see if it frees a pending queue
		 */

		cache->mfc_flags|=MFC_RESOLVED;
		cache->mfc_parent=mfc->mfcc_parent;
		ipmr_update_threshoulds(cache, mfc->mfcc_ttls);
		 
		/*
		 *	Check to see if we resolved a queued list. If so we
		 *	need to send on the frames and tidy up.
		 */
		 
		if(cache->mfc_flags&MFC_QUEUED)
			ipmr_cache_resolve(cache);	/* Unhook & send the frames */
		end_bh_atomic();
		return 0;
	}

	/*
	 *	Unsolicited update - that's ok, add anyway.
	 */
	 
	
	cache=ipmr_cache_alloc(GFP_ATOMIC);
	if(cache==NULL)
	{
		end_bh_atomic();
		return -ENOMEM;
	}
	cache->mfc_flags=MFC_RESOLVED;
	cache->mfc_origin=mfc->mfcc_origin.s_addr;
	cache->mfc_mcastgrp=mfc->mfcc_mcastgrp.s_addr;
	cache->mfc_parent=mfc->mfcc_parent;
	ipmr_update_threshoulds(cache, mfc->mfcc_ttls);
	ipmr_cache_insert(cache);
	end_bh_atomic();
	return 0;
}

static void mrtsock_destruct(struct sock *sk)
{
	if (sk == mroute_socket) {
		ipv4_devconf.mc_forwarding = 0;

		mroute_socket=NULL;
		synchronize_bh();

		mroute_close(sk);
	}
}

/*
 *	Socket options and virtual interface manipulation. The whole
 *	virtual interface system is a complete heap, but unfortunately
 *	that's how BSD mrouted happens to think. Maybe one day with a proper
 *	MOSPF/PIM router set up we can clean this up.
 */
 
int ip_mroute_setsockopt(struct sock *sk,int optname,char *optval,int optlen)
{
	struct vifctl vif;
	struct mfcctl mfc;
	
	if(optname!=MRT_INIT)
	{
		if(sk!=mroute_socket)
			return -EACCES;
	}
	
	switch(optname)
	{
		case MRT_INIT:
			if(sk->type!=SOCK_RAW || sk->num!=IPPROTO_IGMP)
				return -EOPNOTSUPP;
			if(optlen!=sizeof(int))
				return -ENOPROTOOPT;
			{
				int opt;
				if (get_user(opt,(int *)optval))
					return -EFAULT;
				if (opt != 1)
					return -ENOPROTOOPT;
			}
			if(mroute_socket)
				return -EADDRINUSE;
			mroute_socket=sk;
			ipv4_devconf.mc_forwarding = 1;
			if (ip_ra_control(sk, 1, mrtsock_destruct) == 0)
				return 0;
			mrtsock_destruct(sk);
			return -EADDRINUSE;
		case MRT_DONE:
			return ip_ra_control(sk, 0, NULL);
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
			if(optlen!=sizeof(vif))
				return -EINVAL;
			if (copy_from_user(&vif,optval,sizeof(vif)))
				return -EFAULT; 
			if(vif.vifc_vifi >= MAXVIFS)
				return -ENFILE;
			if(optname==MRT_ADD_VIF)
			{
				struct vif_device *v=&vif_table[vif.vifc_vifi];
				struct device *dev;
				struct in_device *in_dev;

				/* Is vif busy ? */
				if (vifc_map&(1<<vif.vifc_vifi))
					return -EADDRINUSE;

				switch (vif.vifc_flags) {
#ifdef CONFIG_IP_PIMSM
				case VIFF_REGISTER:

				/*
				 * Special Purpose VIF in PIM
				 * All the packets will be sent to the daemon
				 */
					if (reg_vif_num >= 0)
						return -EADDRINUSE;
					reg_vif_num = vif.vifc_vifi;
					dev = ipmr_reg_vif(&vif);
					if (!dev) {
						reg_vif_num = -1;
						return -ENOBUFS;
					}
					break;
#endif
				case VIFF_TUNNEL:	
					dev = ipmr_new_tunnel(&vif);
					if (!dev)
						return -ENOBUFS;
					break;
				case 0:	
					dev=ip_dev_find(vif.vifc_lcl_addr.s_addr);
					if (!dev)
						return -EADDRNOTAVAIL;
					break;
				default:
#if 0
					printk(KERN_DEBUG "ipmr_add_vif: flags %02x\n", vif.vifc_flags);
#endif
					return -EINVAL;
				}

				if ((in_dev = dev->ip_ptr) == NULL)
					return -EADDRNOTAVAIL;
				if (in_dev->cnf.mc_forwarding)
					return -EADDRINUSE;
				in_dev->cnf.mc_forwarding = 1;
				dev_set_allmulti(dev, +1);
				ip_rt_multicast_event(in_dev);

				/*
				 *	Fill in the VIF structures
				 */
				start_bh_atomic();
				v->rate_limit=vif.vifc_rate_limit;
				v->local=vif.vifc_lcl_addr.s_addr;
				v->remote=vif.vifc_rmt_addr.s_addr;
				v->flags=vif.vifc_flags;
				v->threshold=vif.vifc_threshold;
				v->dev=dev;
				v->bytes_in = 0;
				v->bytes_out = 0;
				v->pkt_in = 0;
				v->pkt_out = 0;
				v->link = dev->ifindex;
				if (vif.vifc_flags&(VIFF_TUNNEL|VIFF_REGISTER))
					v->link = dev->iflink;
				vifc_map|=(1<<vif.vifc_vifi);
				if (vif.vifc_vifi+1 > maxvif)
					maxvif = vif.vifc_vifi+1;
				end_bh_atomic();
				return 0;
			} else {
				int ret;
				rtnl_lock();
				ret = vif_delete(vif.vifc_vifi);
				rtnl_unlock();
				return ret;
			}

		/*
		 *	Manipulate the forwarding caches. These live
		 *	in a sort of kernel/user symbiosis.
		 */
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
			if(optlen!=sizeof(mfc))
				return -EINVAL;
			if (copy_from_user(&mfc,optval, sizeof(mfc)))
				return -EFAULT;
			return ipmr_mfc_modify(optname, &mfc);
		/*
		 *	Control PIM assert.
		 */
		case MRT_ASSERT:
		{
			int v;
			if(get_user(v,(int *)optval))
				return -EFAULT;
			mroute_do_assert=(v)?1:0;
			return 0;
		}
#ifdef CONFIG_IP_PIMSM
		case MRT_PIM:
		{
			int v;
			if(get_user(v,(int *)optval))
				return -EFAULT;
			v = (v)?1:0;
			if (v != mroute_do_pim) {
				mroute_do_pim = v;
				mroute_do_assert = v;
#ifdef CONFIG_IP_PIMSM_V2
				if (mroute_do_pim)
					inet_add_protocol(&pim_protocol);
				else
					inet_del_protocol(&pim_protocol);
#endif
			}
			return 0;
		}
#endif
		/*
		 *	Spurious command, or MRT_VERSION which you cannot
		 *	set.
		 */
		default:
			return -ENOPROTOOPT;
	}
}

/*
 *	Getsock opt support for the multicast routing system.
 */
 
int ip_mroute_getsockopt(struct sock *sk,int optname,char *optval,int *optlen)
{
	int olr;
	int val;

	if(sk!=mroute_socket)
		return -EACCES;
	if(optname!=MRT_VERSION && 
#ifdef CONFIG_IP_PIMSM
	   optname!=MRT_PIM &&
#endif
	   optname!=MRT_ASSERT)
		return -ENOPROTOOPT;
	
	if(get_user(olr, optlen))
		return -EFAULT;

	olr=min(olr,sizeof(int));
	if(put_user(olr,optlen))
		return -EFAULT;
	if(optname==MRT_VERSION)
		val=0x0305;
#ifdef CONFIG_IP_PIMSM
	else if(optname==MRT_PIM)
		val=mroute_do_pim;
#endif
	else
		val=mroute_do_assert;
	if(copy_to_user(optval,&val,olr))
		return -EFAULT;
	return 0;
}

/*
 *	The IP multicast ioctl support routines.
 */
 
int ipmr_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	struct sioc_sg_req sr;
	struct sioc_vif_req vr;
	struct vif_device *vif;
	struct mfc_cache *c;
	
	switch(cmd)
	{
		case SIOCGETVIFCNT:
			if (copy_from_user(&vr,(void *)arg,sizeof(vr)))
				return -EFAULT; 
			if(vr.vifi>=maxvif)
				return -EINVAL;
			vif=&vif_table[vr.vifi];
			if(vifc_map&(1<<vr.vifi))
			{
				vr.icount=vif->pkt_in;
				vr.ocount=vif->pkt_out;
				vr.ibytes=vif->bytes_in;
				vr.obytes=vif->bytes_out;
				if (copy_to_user((void *)arg,&vr,sizeof(vr)))
					return -EFAULT;
				return 0;
			}
			return -EADDRNOTAVAIL;
		case SIOCGETSGCNT:
			if (copy_from_user(&sr,(void *)arg,sizeof(sr)))
				return -EFAULT; 
			for (c = mfc_cache_array[MFC_HASH(sr.grp.s_addr, sr.src.s_addr)];
			     c; c = c->next) {
				if (sr.grp.s_addr == c->mfc_mcastgrp &&
				    sr.src.s_addr == c->mfc_origin) {
					sr.pktcnt = c->mfc_pkt;
					sr.bytecnt = c->mfc_bytes;
					sr.wrong_if = c->mfc_wrong_if;
					if (copy_to_user((void *)arg,&sr,sizeof(sr)))
						return -EFAULT;
					return 0;
				}
			}
			return -EADDRNOTAVAIL;
		default:
			return -ENOIOCTLCMD;
	}
}

/*
 *	Close the multicast socket, and clear the vif tables etc
 */
 
void mroute_close(struct sock *sk)
{
	int i;
		
	/*
	 *	Shut down all active vif entries
	 */
	rtnl_lock();
	for(i=0; i<maxvif; i++)
		vif_delete(i);
	rtnl_unlock();

	/*
	 *	Wipe the cache
	 */
	for(i=0;i<MFC_LINES;i++)
	{
		start_bh_atomic();
		while(mfc_cache_array[i]!=NULL)
			ipmr_cache_delete(mfc_cache_array[i]);
		end_bh_atomic();
	}
}

static int ipmr_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct vif_device *v;
	int ct;
	if (event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;
	v=&vif_table[0];
	for(ct=0;ct<maxvif;ct++) {
		if (vifc_map&(1<<ct) && v->dev==ptr)
			vif_delete(ct);
		v++;
	}
	return NOTIFY_DONE;
}


static struct notifier_block ip_mr_notifier={
	ipmr_device_event,
	NULL,
	0
};

/*
 * 	Encapsulate a packet by attaching a valid IPIP header to it.
 *	This avoids tunnel drivers and other mess and gives us the speed so
 *	important for multicast video.
 */
 
static void ip_encap(struct sk_buff *skb, u32 saddr, u32 daddr)
{
	struct iphdr *iph = (struct iphdr *)skb_push(skb,sizeof(struct iphdr));

	iph->version	= 	4;
	iph->tos	=	skb->nh.iph->tos;
	iph->ttl	=	skb->nh.iph->ttl;
	iph->frag_off	=	0;
	iph->daddr	=	daddr;
	iph->saddr	=	saddr;
	iph->protocol	=	IPPROTO_IPIP;
	iph->ihl	=	5;
	iph->tot_len	=	htons(skb->len);
	iph->id		=	htons(ip_id_count++);
	ip_send_check(iph);

	skb->h.ipiph = skb->nh.iph;
	skb->nh.iph = iph;
}

/*
 *	Processing handlers for ipmr_forward
 */

static void ipmr_queue_xmit(struct sk_buff *skb, struct mfc_cache *c,
			   int vifi, int last)
{
	struct iphdr *iph = skb->nh.iph;
	struct vif_device *vif = &vif_table[vifi];
	struct device *dev;
	struct rtable *rt;
	int    encap = 0;
	struct sk_buff *skb2;

#ifdef CONFIG_IP_PIMSM
	if (vif->flags & VIFF_REGISTER) {
		vif->pkt_out++;
		vif->bytes_out+=skb->len;
		((struct net_device_stats*)vif->dev->priv)->tx_bytes += skb->len;
		((struct net_device_stats*)vif->dev->priv)->tx_packets++;
		ipmr_cache_report(skb, vifi, IGMPMSG_WHOLEPKT);
		return;
	}
#endif

	if (vif->flags&VIFF_TUNNEL) {
		if (ip_route_output(&rt, vif->remote, vif->local, RT_TOS(iph->tos), vif->link))
			return;
		encap = sizeof(struct iphdr);
	} else {
		if (ip_route_output(&rt, iph->daddr, 0, RT_TOS(iph->tos), vif->link))
			return;
	}

	dev = rt->u.dst.dev;

	if (skb->len+encap > rt->u.dst.pmtu && (ntohs(iph->frag_off) & IP_DF)) {
		/* Do not fragment multicasts. Alas, IPv4 does not
		   allow to send ICMP, so that packets will disappear
		   to blackhole.
		 */

		ip_statistics.IpFragFails++;
		ip_rt_put(rt);
		return;
	}

	encap += dev->hard_header_len;

	if (skb_headroom(skb) < encap || skb_cloned(skb) || !last)
		skb2 = skb_realloc_headroom(skb, (encap + 15)&~15);
	else if (atomic_read(&skb->users) != 1)
		skb2 = skb_clone(skb, GFP_ATOMIC);
	else {
		atomic_inc(&skb->users);
		skb2 = skb;
	}

	if (skb2 == NULL) {
		ip_rt_put(rt);
		return;
	}

	vif->pkt_out++;
	vif->bytes_out+=skb->len;

	dst_release(skb2->dst);
	skb2->dst = &rt->u.dst;
	iph = skb2->nh.iph;
	ip_decrease_ttl(iph);

#ifdef CONFIG_FIREWALL
	if (call_fw_firewall(PF_INET, vif->dev, skb2->nh.iph, NULL, &skb2) < FW_ACCEPT) {
		kfree_skb(skb2);
		return;
	}
	if (call_out_firewall(PF_INET, vif->dev, skb2->nh.iph, NULL, &skb2) < FW_ACCEPT) {
		kfree_skb(skb2);
		return;
	}
#endif
	if (vif->flags & VIFF_TUNNEL) {
		ip_encap(skb2, vif->local, vif->remote);
#ifdef CONFIG_FIREWALL
		/* Double output firewalling on tunnels: one is on tunnel
		   another one is on real device.
		 */
		if (call_out_firewall(PF_INET, dev, skb2->nh.iph, NULL, &skb2) < FW_ACCEPT) {
			kfree_skb(skb2);
			return;
		}
#endif
		((struct ip_tunnel *)vif->dev->priv)->stat.tx_packets++;
		((struct ip_tunnel *)vif->dev->priv)->stat.tx_bytes+=skb2->len;
	}

	IPCB(skb2)->flags |= IPSKB_FORWARDED;


	/*
	 * RFC1584 teaches, that DVMRP/PIM router must deliver packets locally
	 * not only before forwarding, but after forwarding on all output
	 * interfaces. It is clear, if mrouter runs a multicasting
	 * program, it should receive packets not depending to what interface
	 * program is joined.
	 * If we will not make it, the program will have to join on all
	 * interfaces. On the other hand, multihoming host (or router, but
	 * not mrouter) cannot join to more than one interface - it will
	 * result in receiving multiple packets.
	 */
	if (skb2->len <= rt->u.dst.pmtu)
		skb2->dst->output(skb2);
	else
		ip_fragment(skb2, skb2->dst->output);
}

int ipmr_find_vif(struct device *dev)
{
	int ct;
	for (ct=0; ct<maxvif; ct++) {
		if (vifc_map&(1<<ct) && vif_table[ct].dev == dev)
			return ct;
	}
	return ALL_VIFS;
}

/* "local" means that we should preserve one skb (for local delivery) */

int ip_mr_forward(struct sk_buff *skb, struct mfc_cache *cache, int local)
{
	int psend = -1;
	int vif, ct;

	vif = cache->mfc_parent;
	cache->mfc_pkt++;
	cache->mfc_bytes += skb->len;

	/*
	 * Wrong interface: drop packet and (maybe) send PIM assert.
	 */
	if (vif_table[vif].dev != skb->dev) {
		int true_vifi;

		if (((struct rtable*)skb->dst)->key.iif == 0) {
			/* It is our own packet, looped back.
			   Very complicated situation...

			   The best workaround until routing daemons will be
			   fixed is not to redistribute packet, if it was
			   send through wrong interface. It means, that
			   multicast applications WILL NOT work for
			   (S,G), which have default multicast route pointing
			   to wrong oif. In any case, it is not a good
			   idea to use multicasting applications on router.
			 */
			goto dont_forward;
		}

		cache->mfc_wrong_if++;
		true_vifi = ipmr_find_vif(skb->dev);

		if (true_vifi < MAXVIFS && mroute_do_assert &&
		    /* pimsm uses asserts, when switching from RPT to SPT,
		       so that we cannot check that packet arrived on an oif.
		       It is bad, but otherwise we would need to move pretty
		       large chunk of pimd to kernel. Ough... --ANK
		     */
		    (mroute_do_pim || cache->mfc_ttls[true_vifi] < 255) &&
		    jiffies - cache->mfc_last_assert > MFC_ASSERT_THRESH) {
			cache->mfc_last_assert = jiffies;
			ipmr_cache_report(skb, true_vifi, IGMPMSG_WRONGVIF);
		}
		goto dont_forward;
	}

	vif_table[vif].pkt_in++;
	vif_table[vif].bytes_in+=skb->len;

	/*
	 *	Forward the frame
	 */
	for (ct = cache->mfc_maxvif-1; ct >= cache->mfc_minvif; ct--) {
		if (skb->nh.iph->ttl > cache->mfc_ttls[ct]) {
			if (psend != -1)
				ipmr_queue_xmit(skb, cache, psend, 0);
			psend=ct;
		}
	}
	if (psend != -1)
		ipmr_queue_xmit(skb, cache, psend, !local);

dont_forward:
	if (!local)
		kfree_skb(skb);
	return 0;
}


/*
 *	Multicast packets for forwarding arrive here
 */

int ip_mr_input(struct sk_buff *skb)
{
	struct mfc_cache *cache;
	int local = ((struct rtable*)skb->dst)->rt_flags&RTCF_LOCAL;

	/* Packet is looped back after forward, it should not be
	   forwarded second time, but still can be delivered locally.
	 */
	if (IPCB(skb)->flags&IPSKB_FORWARDED)
		goto dont_forward;

	if (!local) {
		    if (IPCB(skb)->opt.router_alert) {
			    if (ip_call_ra_chain(skb))
				    return 0;
		    } else if (skb->nh.iph->protocol == IPPROTO_IGMP && mroute_socket) {
			    /* IGMPv1 (and broken IGMPv2 implementations sort of
			       Cisco IOS <= 11.2(8)) do not put router alert
			       option to IGMP packets destined to routable
			       groups. It is very bad, because it means
			       that we can forward NO IGMP messages.
			     */
			    raw_rcv(mroute_socket, skb);
			    return 0;
		    }
	}

	cache = ipmr_cache_find(skb->nh.iph->saddr, skb->nh.iph->daddr);

	/*
	 *	No usable cache entry
	 */

	if (cache==NULL || (cache->mfc_flags&MFC_QUEUED)) {
		int vif;

		if (local) {
			struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
			ip_local_deliver(skb);
			if (skb2 == NULL)
				return -ENOBUFS;
			skb = skb2;
		}

		vif = ipmr_find_vif(skb->dev);
		if (vif != ALL_VIFS) {
			ipmr_cache_unresolved(cache, vif, skb);
			return -EAGAIN;
		}
		kfree_skb(skb);
		return 0;
	}

	ip_mr_forward(skb, cache, local);

	if (local)
		return ip_local_deliver(skb);
	return 0;

dont_forward:
	if (local)
		return ip_local_deliver(skb);
	kfree_skb(skb);
	return 0;
}

#ifdef CONFIG_IP_PIMSM_V1
/*
 * Handle IGMP messages of PIMv1
 */

int pim_rcv_v1(struct sk_buff * skb, unsigned short len)
{
	struct igmphdr *pim = (struct igmphdr*)skb->h.raw;
	struct iphdr   *encap;

        if (!mroute_do_pim ||
	    len < sizeof(*pim) + sizeof(*encap) ||
	    pim->group != PIM_V1_VERSION || pim->code != PIM_V1_REGISTER ||
	    reg_dev == NULL) {
		kfree_skb(skb);
                return -EINVAL;
        }

	encap = (struct iphdr*)(skb->h.raw + sizeof(struct igmphdr));
	/*
	   Check that:
	   a. packet is really destinted to a multicast group
	   b. packet is not a NULL-REGISTER
	   c. packet is not truncated
	 */
	if (!MULTICAST(encap->daddr) ||
	    ntohs(encap->tot_len) == 0 ||
	    ntohs(encap->tot_len) + sizeof(*pim) > len) {
		kfree_skb(skb);
		return -EINVAL;
	}
	skb->mac.raw = skb->nh.raw;
	skb_pull(skb, (u8*)encap - skb->data);
	skb->nh.iph = (struct iphdr *)skb->data;
	skb->dev = reg_dev;
	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	skb->protocol = __constant_htons(ETH_P_IP);
	skb->ip_summed = 0;
	skb->pkt_type = PACKET_HOST;
	dst_release(skb->dst);
	skb->dst = NULL;
	((struct net_device_stats*)reg_dev->priv)->rx_bytes += skb->len;
	((struct net_device_stats*)reg_dev->priv)->rx_packets++;
	netif_rx(skb);
	return 0;
}
#endif

#ifdef CONFIG_IP_PIMSM_V2
int pim_rcv(struct sk_buff * skb, unsigned short len)
{
	struct pimreghdr *pim = (struct pimreghdr*)skb->h.raw;
	struct iphdr   *encap;

        if (len < sizeof(*pim) + sizeof(*encap) ||
	    pim->type != ((PIM_VERSION<<4)|(PIM_REGISTER)) ||
	    (pim->flags&PIM_NULL_REGISTER) ||
	    reg_dev == NULL ||
	    (ip_compute_csum((void *)pim, sizeof(*pim)) &&
	     ip_compute_csum((void *)pim, len))) {
		kfree_skb(skb);
                return -EINVAL;
        }

	/* check if the inner packet is destined to mcast group */
	encap = (struct iphdr*)(skb->h.raw + sizeof(struct pimreghdr));
	if (!MULTICAST(encap->daddr) ||
	    ntohs(encap->tot_len) == 0 ||
	    ntohs(encap->tot_len) + sizeof(*pim) > len) {
		kfree_skb(skb);
		return -EINVAL;
	}
	skb->mac.raw = skb->nh.raw;
	skb_pull(skb, (u8*)encap - skb->data);
	skb->nh.iph = (struct iphdr *)skb->data;
	skb->dev = reg_dev;
	memset(&(IPCB(skb)->opt), 0, sizeof(struct ip_options));
	skb->protocol = __constant_htons(ETH_P_IP);
	skb->ip_summed = 0;
	skb->pkt_type = PACKET_HOST;
	dst_release(skb->dst);
	((struct net_device_stats*)reg_dev->priv)->rx_bytes += skb->len;
	((struct net_device_stats*)reg_dev->priv)->rx_packets++;
	skb->dst = NULL;
	netif_rx(skb);
	return 0;
}
#endif

#ifdef CONFIG_RTNETLINK

static int
ipmr_fill_mroute(struct sk_buff *skb, struct mfc_cache *c, struct rtmsg *rtm)
{
	int ct;
	struct rtnexthop *nhp;
	struct device *dev = vif_table[c->mfc_parent].dev;
	u8 *b = skb->tail;
	struct rtattr *mp_head;

	if (dev)
		RTA_PUT(skb, RTA_IIF, 4, &dev->ifindex);

	mp_head = (struct rtattr*)skb_put(skb, RTA_LENGTH(0));

	for (ct = c->mfc_minvif; ct < c->mfc_maxvif; ct++) {
		if (c->mfc_ttls[ct] < 255) {
			if (skb_tailroom(skb) < RTA_ALIGN(RTA_ALIGN(sizeof(*nhp)) + 4))
				goto rtattr_failure;
			nhp = (struct rtnexthop*)skb_put(skb, RTA_ALIGN(sizeof(*nhp)));
			nhp->rtnh_flags = 0;
			nhp->rtnh_hops = c->mfc_ttls[ct];
			nhp->rtnh_ifindex = vif_table[ct].dev->ifindex;
			nhp->rtnh_len = sizeof(*nhp);
		}
	}
	mp_head->rta_type = RTA_MULTIPATH;
	mp_head->rta_len = skb->tail - (u8*)mp_head;
	rtm->rtm_type = RTN_MULTICAST;
	return 1;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -EMSGSIZE;
}

int ipmr_get_route(struct sk_buff *skb, struct rtmsg *rtm, int nowait)
{
	struct mfc_cache *cache;
	struct rtable *rt = (struct rtable*)skb->dst;

	start_bh_atomic();
	cache = ipmr_cache_find(rt->rt_src, rt->rt_dst);
	if (cache==NULL || (cache->mfc_flags&MFC_QUEUED)) {
		struct device *dev;
		int vif;
		int err;

		if (nowait) {
			end_bh_atomic();
			return -EAGAIN;
		}

		dev = skb->dev;
		if (dev == NULL || (vif = ipmr_find_vif(dev)) == ALL_VIFS) {
			end_bh_atomic();
			return -ENODEV;
		}
		skb->nh.raw = skb_push(skb, sizeof(struct iphdr));
		skb->nh.iph->ihl = sizeof(struct iphdr)>>2;
		skb->nh.iph->saddr = rt->rt_src;
		skb->nh.iph->daddr = rt->rt_dst;
		skb->nh.iph->version = 0;
		err = ipmr_cache_unresolved(cache, vif, skb);
		end_bh_atomic();
		return err;
	}
	/* Resolved cache entry is not changed by net bh,
	   so that we are allowed to enable it.
	 */
	end_bh_atomic();

	if (!nowait && (rtm->rtm_flags&RTM_F_NOTIFY))
		cache->mfc_flags |= MFC_NOTIFY;
	return ipmr_fill_mroute(skb, cache, rtm);
}
#endif

/*
 *	The /proc interfaces to multicast routing /proc/ip_mr_cache /proc/ip_mr_vif
 */
 
int ipmr_vif_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct vif_device *vif;
	int len=0;
	off_t pos=0;
	off_t begin=0;
	int size;
	int ct;

	len += sprintf(buffer,
		 "Interface      BytesIn  PktsIn  BytesOut PktsOut Flags Local    Remote\n");
	pos=len;
  
	for (ct=0;ct<maxvif;ct++) 
	{
		char *name = "none";
		vif=&vif_table[ct];
		if(!(vifc_map&(1<<ct)))
			continue;
		if (vif->dev)
			name = vif->dev->name;
        	size = sprintf(buffer+len, "%2d %-10s %8ld %7ld  %8ld %7ld %05X %08X %08X\n",
        		ct, name, vif->bytes_in, vif->pkt_in, vif->bytes_out, vif->pkt_out,
        		vif->flags, vif->local, vif->remote);
		len+=size;
		pos+=size;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
  	}
  	
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
  	return len;
}

int ipmr_mfc_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	struct mfc_cache *mfc;
	int len=0;
	off_t pos=0;
	off_t begin=0;
	int size;
	int ct;

	len += sprintf(buffer,
		 "Group    Origin   Iif     Pkts    Bytes    Wrong Oifs\n");
	pos=len;
  
	for (ct=0;ct<MFC_LINES;ct++) 
	{
		start_bh_atomic();
		mfc=mfc_cache_array[ct];
		while(mfc!=NULL)
		{
			int n;

			/*
			 *	Interface forwarding map
			 */
			size = sprintf(buffer+len, "%08lX %08lX %-3d %8ld %8ld %8ld",
				(unsigned long)mfc->mfc_mcastgrp,
				(unsigned long)mfc->mfc_origin,
				mfc->mfc_parent == ALL_VIFS ? -1 : mfc->mfc_parent,
				(mfc->mfc_flags & MFC_QUEUED) ? mfc->mfc_unresolved.qlen : mfc->mfc_pkt,
				mfc->mfc_bytes,
				mfc->mfc_wrong_if);
			for(n=mfc->mfc_minvif;n<mfc->mfc_maxvif;n++)
			{
				if(vifc_map&(1<<n) && mfc->mfc_ttls[n] < 255)
					size += sprintf(buffer+len+size, " %2d:%-3d", n, mfc->mfc_ttls[n]);
			}
			size += sprintf(buffer+len+size, "\n");
			len+=size;
			pos+=size;
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
			{
				end_bh_atomic();
				goto done;
			}
			mfc=mfc->next;
	  	}
	  	end_bh_atomic();
  	}
done:
  	*start=buffer+(offset-begin);
  	len-=(offset-begin);
  	if(len>length)
  		len=length;
	if (len < 0) {
		len = 0;
	}
  	return len;
}

#ifdef CONFIG_PROC_FS	
static struct proc_dir_entry proc_net_ipmr_vif = {
	PROC_NET_IPMR_VIF, 9 ,"ip_mr_vif",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ipmr_vif_info
};
static struct proc_dir_entry proc_net_ipmr_mfc = {
	PROC_NET_IPMR_MFC, 11 ,"ip_mr_cache",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ipmr_mfc_info
};
#endif	

#ifdef CONFIG_IP_PIMSM_V2
struct inet_protocol pim_protocol = 
{
	pim_rcv,		/* PIM handler		*/
	NULL,			/* PIM error control	*/
	NULL,			/* next			*/
	IPPROTO_PIM,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"PIM"			/* name			*/
};
#endif


/*
 *	Setup for IP multicast routing
 */
 
__initfunc(void ip_mr_init(void))
{
	printk(KERN_INFO "Linux IP multicast router 0.06 plus PIM-SM\n");
	register_netdevice_notifier(&ip_mr_notifier);
#ifdef CONFIG_PROC_FS	
	proc_net_register(&proc_net_ipmr_vif);
	proc_net_register(&proc_net_ipmr_mfc);
#endif	
}
