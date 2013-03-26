/*
 * 	NET3	Protocol independent device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dhinds@allegro.stanford.edu>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *
 *	Changes:
 *		Marcelo Tosatti <marcelo@conectiva.com.br> : dont accept mtu 0 or <
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant stunts to
 *					keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before calling netif_rx. Saves
 *					a function call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close changes.
 *		Rudi Cilibrasi	:	Pass the right thing to set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to make it work out
 *					on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russel	:	SIOCSIFNAME
 *	Andrea Arcangeli	:	dev_clear_backlog() needs the
 *					skb_queue_lock held.
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <net/slhc.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <net/br.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/profile.h>
#include <linux/init.h>
#include <linux/kmod.h>
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>
#endif	/* CONFIG_NET_RADIO */
#ifdef CONFIG_PLIP
extern int plip_init(void);
#endif

NET_PROFILE_DEFINE(dev_queue_xmit)
NET_PROFILE_DEFINE(net_bh)
NET_PROFILE_DEFINE(net_bh_skb)


const char *if_port_text[] = {
  "unknown",
  "BNC",
  "10baseT",
  "AUI",
  "100baseT",
  "100baseTX",
  "100baseFX"
};

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25.
 *
 *		0800	IP
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */

struct packet_type *ptype_base[16];		/* 16 way hashed list */
struct packet_type *ptype_all = NULL;		/* Taps */

/*
 *	Device list lock. Setting it provides that interface
 *	will not disappear unexpectedly while kernel sleeps.
 */

atomic_t dev_lockct = ATOMIC_INIT(0);

/*
 *	Our notifier list
 */

#ifdef _HURD_
struct notifier_block *netdev_chain=NULL;
#else
static struct notifier_block *netdev_chain=NULL;
#endif

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the bottom half handler.
 */

static struct sk_buff_head backlog;

#ifdef CONFIG_NET_FASTROUTE
int netdev_fastroute;
int netdev_fastroute_obstacles;
struct net_fastroute_stats dev_fastroute_stat;
#endif

static void dev_clear_backlog(struct device *dev);


/******************************************************************************************

		Protocol management and registration routines

*******************************************************************************************/

/*
 *	For efficiency
 */

int netdev_nit=0;

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscuous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explantion follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */

void dev_add_pack(struct packet_type *pt)
{
	int hash;
#ifdef CONFIG_NET_FASTROUTE
	/* Hack to detect packet socket */
	if (pt->data) {
		netdev_fastroute_obstacles++;
		dev_clear_fastroute(pt->dev);
	}
#endif
	if(pt->type==htons(ETH_P_ALL))
	{
		netdev_nit++;
		pt->next=ptype_all;
		ptype_all=pt;
	}
	else
	{
		hash=ntohs(pt->type)&15;
		pt->next = ptype_base[hash];
		ptype_base[hash] = pt;
	}
}


/*
 *	Remove a protocol ID from the list.
 */

void dev_remove_pack(struct packet_type *pt)
{
	struct packet_type **pt1;
	if(pt->type==htons(ETH_P_ALL))
	{
		netdev_nit--;
		pt1=&ptype_all;
	}
	else
		pt1=&ptype_base[ntohs(pt->type)&15];
	for(; (*pt1)!=NULL; pt1=&((*pt1)->next))
	{
		if(pt==(*pt1))
		{
			*pt1=pt->next;
			synchronize_bh();
#ifdef CONFIG_NET_FASTROUTE
			if (pt->data)
				netdev_fastroute_obstacles--;
#endif
			return;
		}
	}
	printk(KERN_WARNING "dev_remove_pack: %p not found.\n", pt);
}

/*****************************************************************************************

			    Device Interface Subroutines

******************************************************************************************/

/*
 *	Find an interface by name.
 */

struct device *dev_get(const char *name)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next)
	{
		if (strcmp(dev->name, name) == 0)
			return(dev);
	}
	return NULL;
}

struct device * dev_get_by_index(int ifindex)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next)
	{
		if (dev->ifindex == ifindex)
			return(dev);
	}
	return NULL;
}

struct device *dev_getbyhwaddr(unsigned short type, char *ha)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next)
	{
		if (dev->type == type &&
		    memcmp(dev->dev_addr, ha, dev->addr_len) == 0)
			return(dev);
	}
	return(NULL);
}

/*
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. Not efficient for many devices, not called a lot..
 */

int dev_alloc_name(struct device *dev, const char *name)
{
	int i;
	/*
	 *	If you need over 100 please also fix the algorithm...
	 */
	for(i=0;i<100;i++)
	{
		sprintf(dev->name,name,i);
		if(dev_get(dev->name)==NULL)
			return i;
	}
	return -ENFILE;	/* Over 100 of the things .. bail out! */
}

struct device *dev_alloc(const char *name, int *err)
{
	struct device *dev=kmalloc(sizeof(struct device)+16, GFP_KERNEL);
	if(dev==NULL)
	{
		*err=-ENOBUFS;
		return NULL;
	}
	dev->name=(char *)(dev+1);	/* Name string space */
	*err=dev_alloc_name(dev,name);
	if(*err<0)
	{
		kfree(dev);
		return NULL;
	}
	return dev;
}

void netdev_state_change(struct device *dev)
{
	if (dev->flags&IFF_UP)
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);
}


/*
 *	Find and possibly load an interface.
 */

#ifdef CONFIG_KMOD

void dev_load(const char *name)
{
	if(!dev_get(name) && capable(CAP_SYS_MODULE))
		request_module(name);
}

#else

extern inline void dev_load(const char *unused){;}

#endif

static int default_rebuild_header(struct sk_buff *skb)
{
	printk(KERN_DEBUG "%s: default_rebuild_header called -- BUG!\n", skb->dev ? skb->dev->name : "NULL!!!");
	kfree_skb(skb);
	return 1;
}

/*
 *	Prepare an interface for use.
 */

int dev_open(struct device *dev)
{
	int ret = 0;

	/*
	 *	Is it already up?
	 */

	if (dev->flags&IFF_UP)
		return 0;

	/*
	 *	Call device private open method
	 */

	if (dev->open)
  		ret = dev->open(dev);

	/*
	 *	If it went open OK then:
	 */

	if (ret == 0)
	{
		/*
		 *	nil rebuild_header routine,
		 *	that should be never called and used as just bug trap.
		 */

		if (dev->rebuild_header == NULL)
			dev->rebuild_header = default_rebuild_header;

		/*
		 *	Set the flags.
		 */
		dev->flags |= (IFF_UP | IFF_RUNNING);

		/*
		 *	Initialize multicasting status
		 */
		dev_mc_upload(dev);

		/*
		 *	Wakeup transmit queue engine
		 */
		dev_activate(dev);

		/*
		 *	... and announce new interface.
		 */
		notifier_call_chain(&netdev_chain, NETDEV_UP, dev);

	}
	return(ret);
}

#ifdef CONFIG_NET_FASTROUTE

static __inline__ void dev_do_clear_fastroute(struct device *dev)
{
	if (dev->accept_fastpath) {
		int i;

		for (i=0; i<=NETDEV_FASTROUTE_HMASK; i++)
			dst_release_irqwait(xchg(dev->fastpath+i, NULL));
	}
}

void dev_clear_fastroute(struct device *dev)
{
	if (dev) {
		dev_do_clear_fastroute(dev);
	} else {
		for (dev = dev_base; dev; dev = dev->next)
			dev_do_clear_fastroute(dev);
	}
}
#endif

/*
 *	Completely shutdown an interface.
 */

int dev_close(struct device *dev)
{
	if (!(dev->flags&IFF_UP))
		return 0;

	dev_deactivate(dev);

	dev_lock_wait();

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 */

	if (dev->stop)
		dev->stop(dev);

	if (dev->start)
		printk("dev_close: bug %s still running\n", dev->name);

	/*
	 *	Device is now down.
	 */
	dev_clear_backlog(dev);

	dev->flags&=~(IFF_UP|IFF_RUNNING);
#ifdef CONFIG_NET_FASTROUTE
	dev_clear_fastroute(dev);
#endif

	/*
	 *	Tell people we are going down
	 */
	notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);

	return(0);
}


/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netdev_chain, nb);
}

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netdev_chain,nb);
}

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

void dev_queue_xmit_nit(struct sk_buff *skb, struct device *dev)
{
	struct packet_type *ptype;
	get_fast_time(&skb->stamp);

	for (ptype = ptype_all; ptype!=NULL; ptype = ptype->next)
	{
		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if ((ptype->dev == dev || !ptype->dev) &&
			((struct sock *)ptype->data != skb->sk))
		{
			struct sk_buff *skb2;
			if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL)
				break;

			/* Code, following below is wrong.

			   The only reason, why it does work is that
			   ONLY packet sockets receive outgoing
			   packets. If such a packet will be (occasionally)
			   received by normal packet handler, which expects
			   that mac header is pulled...
			 */

			/* More sensible variant. skb->nh should be correctly
			   set by sender, so that the second statement is
			   just protection against buggy protocols.
			 */
			skb2->mac.raw = skb2->data;

			if (skb2->nh.raw < skb2->data || skb2->nh.raw >= skb2->tail) {
				if (net_ratelimit())
					printk(KERN_DEBUG "protocol %04x is buggy, dev %s\n", skb2->protocol, dev->name);
				skb2->nh.raw = skb2->data;
				if (dev->hard_header)
					skb2->nh.raw += dev->hard_header_len;
			}

			skb2->h.raw = skb2->nh.raw;
			skb2->pkt_type = PACKET_OUTGOING;
			ptype->func(skb2, skb->dev, ptype);
		}
	}
}

/*
 *	Fast path for loopback frames.
 */

void dev_loopback_xmit(struct sk_buff *skb)
{
	struct sk_buff *newskb=skb_clone(skb, GFP_ATOMIC);
	if (newskb==NULL)
		return;

	newskb->mac.raw = newskb->data;
	skb_pull(newskb, newskb->nh.raw - newskb->data);
	newskb->pkt_type = PACKET_LOOPBACK;
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	if (newskb->dst==NULL)
		printk(KERN_DEBUG "BUG: packet without dst looped back 1\n");
	netif_rx(newskb);
}

int dev_queue_xmit(struct sk_buff *skb)
{
	struct device *dev = skb->dev;
	struct Qdisc  *q;

#ifdef CONFIG_NET_PROFILE
	start_bh_atomic();
	NET_PROFILE_ENTER(dev_queue_xmit);
#endif

	start_bh_atomic();
	q = dev->qdisc;
	if (q->enqueue) {
		q->enqueue(skb, q);
		qdisc_wakeup(dev);
		end_bh_atomic();

#ifdef CONFIG_NET_PROFILE
	        NET_PROFILE_LEAVE(dev_queue_xmit);
		end_bh_atomic();
#endif

		return 0;
	}

	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...

	   Really, it is unlikely that bh protection is necessary here:
	   virtual devices do not generate EOI events.
	   However, it is possible, that they rely on bh protection
	   made by us here.
	 */
	if (dev->flags&IFF_UP) {
		if (netdev_nit)
			dev_queue_xmit_nit(skb,dev);
		if (dev->hard_start_xmit(skb, dev) == 0) {
			end_bh_atomic();

#ifdef CONFIG_NET_PROFILE
			NET_PROFILE_LEAVE(dev_queue_xmit);
			end_bh_atomic();
#endif

			return 0;
		}
		if (net_ratelimit())
			printk(KERN_DEBUG "Virtual device %s asks to queue packet!\n", dev->name);
	}
	end_bh_atomic();

	kfree_skb(skb);

#ifdef CONFIG_NET_PROFILE
	NET_PROFILE_LEAVE(dev_queue_xmit);
	end_bh_atomic();
#endif

	return 0;
}


/*=======================================================================
			Receiver rotutines
  =======================================================================*/

int netdev_dropping = 0;
int netdev_max_backlog = 300;
atomic_t netdev_rx_dropped;
#ifdef CONFIG_CPU_IS_SLOW
int net_cpu_congestion;
#endif

#ifdef CONFIG_NET_HW_FLOWCONTROL
int netdev_throttle_events;
static unsigned long netdev_fc_mask = 1;
unsigned long netdev_fc_xoff = 0;

static struct
{
	void (*stimul)(struct device *);
	struct device *dev;
} netdev_fc_slots[32];

int netdev_register_fc(struct device *dev, void (*stimul)(struct device *dev))
{
	int bit = 0;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (netdev_fc_mask != ~0UL) {
		bit = ffz(netdev_fc_mask);
		netdev_fc_slots[bit].stimul = stimul;
		netdev_fc_slots[bit].dev = dev;
		set_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	restore_flags(flags);
	return bit;
}

void netdev_unregister_fc(int bit)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	if (bit > 0) {
		netdev_fc_slots[bit].stimul = NULL;
		netdev_fc_slots[bit].dev = NULL;
		clear_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	restore_flags(flags);
}

static void netdev_wakeup(void)
{
	unsigned long xoff;

	cli();
	xoff = netdev_fc_xoff;
	netdev_fc_xoff = 0;
	netdev_dropping = 0;
	netdev_throttle_events++;
	while (xoff) {
		int i = ffz(~xoff);
		xoff &= ~(1<<i);
		netdev_fc_slots[i].stimul(netdev_fc_slots[i].dev);
	}
	sti();
}
#endif

static void dev_clear_backlog(struct device *dev)
{
	struct sk_buff *curr;
	unsigned long flags;

	/*
	 *
	 *  Let now clear backlog queue. -AS
	 *
	 *  We are competing here both with netif_rx() and net_bh().
	 *  We don't want either of those to mess with skb ptrs
	 *  while we work on them, thus we must grab the
	 *  skb_queue_lock.
	 */

	if (backlog.qlen) {
	repeat:
		spin_lock_irqsave(&skb_queue_lock, flags);
		for (curr = backlog.next;
		     curr != (struct sk_buff *)(&backlog);
		     curr = curr->next)
			if (curr->dev == dev)
			{
				__skb_unlink(curr, &backlog);
				spin_unlock_irqrestore(&skb_queue_lock, flags);
				kfree_skb(curr);
				goto repeat;
			}
		spin_unlock_irqrestore(&skb_queue_lock, flags);
#ifdef CONFIG_NET_HW_FLOWCONTROL
		if (netdev_dropping)
			netdev_wakeup();
#else
		netdev_dropping = 0;
#endif
	}
}

/*
 *	Receive a packet from a device driver and queue it for the upper
 *	(protocol) levels.  It always succeeds.
 */

void netif_rx(struct sk_buff *skb)
{
#ifndef CONFIG_CPU_IS_SLOW
	if(skb->stamp.tv_sec==0)
		get_fast_time(&skb->stamp);
#else
	skb->stamp = xtime;
#endif

	/* The code is rearranged so that the path is the most
	   short when CPU is congested, but is still operating.
	 */

	if (backlog.qlen <= netdev_max_backlog) {
		if (backlog.qlen) {
			if (netdev_dropping == 0) {
				skb_queue_tail(&backlog,skb);
				mark_bh(NET_BH);
				return;
			}
			atomic_inc(&netdev_rx_dropped);
			kfree_skb(skb);
			return;
		}
#ifdef CONFIG_NET_HW_FLOWCONTROL
		if (netdev_dropping)
			netdev_wakeup();
#else
		netdev_dropping = 0;
#endif
		skb_queue_tail(&backlog,skb);
		mark_bh(NET_BH);
		return;
	}
	netdev_dropping = 1;
	atomic_inc(&netdev_rx_dropped);
	kfree_skb(skb);
}

#ifdef CONFIG_BRIDGE
static inline void handle_bridge(struct sk_buff *skb, unsigned short type)
{
	/*
	 * The br_stats.flags is checked here to save the expense of a
	 * function call.
	 */
	if ((br_stats.flags & BR_UP) && br_call_bridge(skb, type))
	{
		/*
		 *	We pass the bridge a complete frame. This means
		 *	recovering the MAC header first.
		 */

		int offset;

		skb=skb_clone(skb, GFP_ATOMIC);
		if(skb==NULL)
			return;

		offset=skb->data-skb->mac.raw;
		skb_push(skb,offset);	/* Put header back on for bridge */

		if(br_receive_frame(skb))
			return;
		kfree_skb(skb);
	}
	return;
}
#endif

/*
 *	When we are called the queue is ready to grab, the interrupts are
 *	on and hardware can interrupt and queue to the receive queue as we
 *	run with no problems.
 *	This is run as a bottom half after an interrupt handler that does
 *	mark_bh(NET_BH);
 */

void net_bh(void)
{
	struct packet_type *ptype;
	struct packet_type *pt_prev;
	unsigned short type;
#ifndef _HURD_
	unsigned long start_time = jiffies;
#ifdef CONFIG_CPU_IS_SLOW
	static unsigned long start_busy = 0;
	static unsigned long ave_busy = 0;

	if (start_busy == 0)
		start_busy = start_time;
	net_cpu_congestion = ave_busy>>8;
#endif
#endif

	NET_PROFILE_ENTER(net_bh);
	/*
	 *	Can we send anything now? We want to clear the
	 *	decks for any more sends that get done as we
	 *	process the input. This also minimises the
	 *	latency on a transmit interrupt bh.
	 */

	if (qdisc_head.forw != &qdisc_head)
		qdisc_run_queues();

	/*
	 *	Any data left to process. This may occur because a
	 *	mark_bh() is done after we empty the queue including
	 *	that from the device which does a mark_bh() just after
	 */

	/*
	 *	While the queue is not empty..
	 *
	 *	Note that the queue never shrinks due to
	 *	an interrupt, so we can do this test without
	 *	disabling interrupts.
	 */

	while (!skb_queue_empty(&backlog))
	{
		struct sk_buff * skb;

#ifndef _HURD_
		/* Give chance to other bottom halves to run */
		if (jiffies - start_time > 1)
			goto net_bh_break;
#endif

		/*
		 *	We have a packet. Therefore the queue has shrunk
		 */
		skb = skb_dequeue(&backlog);

#ifndef _HURD_
#ifdef CONFIG_CPU_IS_SLOW
		if (ave_busy > 128*16) {
			kfree_skb(skb);
			while ((skb = skb_dequeue(&backlog)) != NULL)
				kfree_skb(skb);
			break;
		}
#endif
#endif


#if 0
		NET_PROFILE_SKB_PASSED(skb, net_bh_skb);
#endif
#ifdef CONFIG_NET_FASTROUTE
		if (skb->pkt_type == PACKET_FASTROUTE) {
			dev_queue_xmit(skb);
			continue;
		}
#endif

		/*
	 	 *	Bump the pointer to the next structure.
		 *
		 *	On entry to the protocol layer. skb->data and
		 *	skb->nh.raw point to the MAC and encapsulated data
		 */

		/* XXX until we figure out every place to modify.. */
		skb->h.raw = skb->nh.raw = skb->data;

		if (skb->mac.raw < skb->head || skb->mac.raw > skb->data) {
			printk(KERN_CRIT "%s: wrong mac.raw ptr, proto=%04x\n", skb->dev->name, skb->protocol);
			kfree_skb(skb);
			continue;
		}

		/*
		 * 	Fetch the packet protocol ID.
		 */

		type = skb->protocol;

#ifdef CONFIG_BRIDGE
		/*
		 *	If we are bridging then pass the frame up to the
		 *	bridging code (if this protocol is to be bridged).
		 *      If it is bridged then move on
		 */
		handle_bridge(skb, type);
#endif

		/*
		 *	We got a packet ID.  Now loop over the "known protocols"
		 * 	list. There are two lists. The ptype_all list of taps (normally empty)
		 *	and the main protocol list which is hashed perfectly for normal protocols.
		 */

		pt_prev = NULL;
		for (ptype = ptype_all; ptype!=NULL; ptype=ptype->next)
		{
			if (!ptype->dev || ptype->dev == skb->dev) {
				if(pt_prev)
				{
					struct sk_buff *skb2=skb_clone(skb, GFP_ATOMIC);
					if(skb2)
						pt_prev->func(skb2,skb->dev, pt_prev);
				}
				pt_prev=ptype;
			}
		}

		for (ptype = ptype_base[ntohs(type)&15]; ptype != NULL; ptype = ptype->next)
		{
			if (ptype->type == type && (!ptype->dev || ptype->dev==skb->dev))
			{
				/*
				 *	We already have a match queued. Deliver
				 *	to it and then remember the new match
				 */
				if(pt_prev)
				{
					struct sk_buff *skb2;

					skb2=skb_clone(skb, GFP_ATOMIC);

					/*
					 *	Kick the protocol handler. This should be fast
					 *	and efficient code.
					 */

					if(skb2)
						pt_prev->func(skb2, skb->dev, pt_prev);
				}
				/* Remember the current last to do */
				pt_prev=ptype;
			}
		} /* End of protocol list loop */

		/*
		 *	Is there a last item to send to ?
		 */

		if(pt_prev)
			pt_prev->func(skb, skb->dev, pt_prev);
		/*
		 * 	Has an unknown packet has been received ?
		 */

		else {
			kfree_skb(skb);
		}
  	}	/* End of queue loop */

  	/*
  	 *	We have emptied the queue
  	 */

	/*
	 *	One last output flush.
	 */

	if (qdisc_head.forw != &qdisc_head)
		qdisc_run_queues();

#ifndef _HURD_
#ifdef  CONFIG_CPU_IS_SLOW
        if (1) {
		unsigned long start_idle = jiffies;
		ave_busy += ((start_idle - start_busy)<<3) - (ave_busy>>4);
		start_busy = 0;
	}
#endif
#endif
#ifdef CONFIG_NET_HW_FLOWCONTROL
	if (netdev_dropping)
		netdev_wakeup();
#else
	netdev_dropping = 0;
#endif
	NET_PROFILE_LEAVE(net_bh);
	return;

#ifndef _HURD_
net_bh_break:
	mark_bh(NET_BH);
	NET_PROFILE_LEAVE(net_bh);
	return;
#endif
}

/* Protocol dependent address dumping routines */

static gifconf_func_t * gifconf_list [NPROTO];

int register_gifconf(unsigned int family, gifconf_func_t * gifconf)
{
	if (family>=NPROTO)
		return -EINVAL;
	gifconf_list[family] = gifconf;
	return 0;
}


/*
 *	Map an interface index to its name (SIOCGIFNAME)
 */

/*
 *	This call is useful, but I'd remove it too.
 *
 *	The reason is purely aestetical, it is the only call
 *	from SIOC* family using struct ifreq in reversed manner.
 *	Besides that, it is pretty silly to put "drawing" facility
 *	to kernel, it is useful only to print ifindices
 *	in readable form, is not it? --ANK
 *
 *	We need this ioctl for efficient implementation of the
 *	if_indextoname() function required by the IPv6 API.  Without
 *	it, we would have to search all the interfaces to find a
 *	match.  --pb
 */

static int dev_ifname(struct ifreq *arg)
{
	struct device *dev;
	struct ifreq ifr;
	int err;

	/*
	 *	Fetch the caller's info block.
	 */

	err = copy_from_user(&ifr, arg, sizeof(struct ifreq));
	if (err)
		return -EFAULT;

	dev = dev_get_by_index(ifr.ifr_ifindex);
	if (!dev)
		return -ENODEV;

	strcpy(ifr.ifr_name, dev->name);

	err = copy_to_user(arg, &ifr, sizeof(struct ifreq));
	return (err)?-EFAULT:0;
}

/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size eventually, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

#ifdef _HURD_
int dev_ifconf(char *arg)
#else
static int dev_ifconf(char *arg)
#endif
{
	struct ifconf ifc;
	struct device *dev;
	char *pos;
	int len;
	int total;
	int i;

	/*
	 *	Fetch the caller's info block.
	 */

	if (copy_from_user(&ifc, arg, sizeof(struct ifconf)))
		return -EFAULT;

	pos = ifc.ifc_buf;
	len = ifc.ifc_len;

	/*
	 *	Loop over the interfaces, and write an info block for each.
	 */

	total = 0;
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		for (i=0; i<NPROTO; i++) {
			if (gifconf_list[i]) {
				int done;
				if (pos==NULL) {
					done = gifconf_list[i](dev, NULL, 0);
				} else {
					done = gifconf_list[i](dev, pos+total, len-total);
				}
				if (done<0)
					return -EFAULT;
				total += done;
			}
		}
  	}

	/*
	 *	All done.  Write the updated control block back to the caller.
	 */
	ifc.ifc_len = total;

	if (copy_to_user(arg, &ifc, sizeof(struct ifconf)))
		return -EFAULT;

	/*
	 * 	Both BSD and Solaris return 0 here, so we do too.
	 */
	return 0;
}

/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */

#ifdef CONFIG_PROC_FS
static int sprintf_stats(char *buffer, struct device *dev)
{
	struct net_device_stats *stats = (dev->get_stats ? dev->get_stats(dev): NULL);
	int size;

	if (stats)
		size = sprintf(buffer, "%6s:%8lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu %8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
 		   dev->name,
		   stats->rx_bytes,
		   stats->rx_packets, stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors
		   + stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->rx_compressed, stats->multicast,
		   stats->tx_bytes,
		   stats->tx_packets, stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors + stats->tx_aborted_errors
		   + stats->tx_window_errors + stats->tx_heartbeat_errors,
		   stats->tx_compressed);
	else
		size = sprintf(buffer, "%6s: No statistics available.\n", dev->name);

	return size;
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized /proc/net interface
 *	to create /proc/net/dev
 */

int dev_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;

	struct device *dev;


	size = sprintf(buffer,
		"Inter-|   Receive                                                |  Transmit\n"
		" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");

	pos+=size;
	len+=size;


	for (dev = dev_base; dev != NULL; dev = dev->next)
	{
		size = sprintf_stats(buffer+len, dev);
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

	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		/* Ending slop */
	return len;
}

static int dev_proc_stats(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	int len;

	len = sprintf(buffer, "%08x %08x %08x %08x %08x\n",
		      atomic_read(&netdev_rx_dropped),
#ifdef CONFIG_NET_HW_FLOWCONTROL
		      netdev_throttle_events,
#else
		      0,
#endif
#ifdef CONFIG_NET_FASTROUTE
		      dev_fastroute_stat.hits,
		      dev_fastroute_stat.succeed,
		      dev_fastroute_stat.deferred
#else
		      0, 0, 0
#endif
		      );

	len -= offset;

	if (len > length)
		len = length;
	if(len < 0)
		len = 0;

	*start = buffer + offset;
	*eof = 1;

	return len;
}

#endif	/* CONFIG_PROC_FS */


#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS

/*
 * Print one entry of /proc/net/wireless
 * This is a clone of /proc/net/dev (just above)
 */
static int sprintf_wireless_stats(char *buffer, struct device *dev)
{
	/* Get stats from the driver */
	struct iw_statistics *stats = (dev->get_wireless_stats ?
				       dev->get_wireless_stats(dev) :
				       (struct iw_statistics *) NULL);
	int size;

	if(stats != (struct iw_statistics *) NULL)
	{
		size = sprintf(buffer,
			       "%6s: %04x  %3d%c  %3d%c  %3d%c  %6d %6d %6d\n",
			       dev->name,
			       stats->status,
			       stats->qual.qual,
			       stats->qual.updated & 1 ? '.' : ' ',
			       stats->qual.level,
			       stats->qual.updated & 2 ? '.' : ' ',
			       stats->qual.noise,
			       stats->qual.updated & 4 ? '.' : ' ',
			       stats->discard.nwid,
			       stats->discard.code,
			       stats->discard.misc);
		stats->qual.updated = 0;
	}
	else
		size = 0;

	return size;
}

/*
 * Print info for /proc/net/wireless (print all entries)
 * This is a clone of /proc/net/dev (just above)
 */
int dev_get_wireless_info(char * buffer, char **start, off_t offset,
			  int length, int dummy)
{
	int		len = 0;
	off_t		begin = 0;
	off_t		pos = 0;
	int		size;

	struct device *	dev;

	size = sprintf(buffer,
		       "Inter-| sta-|   Quality        |   Discarded packets\n"
		       " face | tus | link level noise |  nwid  crypt   misc\n"
			);

	pos+=size;
	len+=size;

	for(dev = dev_base; dev != NULL; dev = dev->next)
	{
		size = sprintf_wireless_stats(buffer+len, dev);
		len+=size;
		pos=begin+len;

		if(pos < offset)
		{
			len=0;
			begin=pos;
		}
		if(pos > offset + length)
			break;
	}

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);		/* Start slop */
	if(len > length)
		len = length;		/* Ending slop */

	return len;
}
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */

void dev_set_promiscuity(struct device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_PROMISC;
	if ((dev->promiscuity += inc) == 0)
		dev->flags &= ~IFF_PROMISC;
	if (dev->flags^old_flags) {
#ifdef CONFIG_NET_FASTROUTE
		if (dev->flags&IFF_PROMISC) {
			netdev_fastroute_obstacles++;
			dev_clear_fastroute(dev);
		} else
			netdev_fastroute_obstacles--;
#endif
		dev_mc_upload(dev);
		printk(KERN_INFO "device %s %s promiscuous mode\n",
		       dev->name, (dev->flags&IFF_PROMISC) ? "entered" : "left");
	}
}

void dev_set_allmulti(struct device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_ALLMULTI;
	if ((dev->allmulti += inc) == 0)
		dev->flags &= ~IFF_ALLMULTI;
	if (dev->flags^old_flags)
		dev_mc_upload(dev);
}

int dev_change_flags(struct device *dev, unsigned flags)
{
	int ret;
	int old_flags = dev->flags;

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG|IFF_NOTRAILERS|IFF_RUNNING|IFF_NOARP|
			       IFF_SLAVE|IFF_MASTER|IFF_DYNAMIC|
			       IFF_MULTICAST|IFF_PORTSEL|IFF_AUTOMEDIA)) |
				       (dev->flags & (IFF_UP|IFF_VOLATILE|IFF_PROMISC|IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */

	dev_mc_upload(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags^flags)&IFF_UP)	/* Bit is different  ? */
	{
		ret = ((old_flags & IFF_UP) ? dev_close : dev_open)(dev);

		if (ret == 0)
			dev_mc_upload(dev);
	}

	if (dev->flags&IFF_UP &&
	    ((old_flags^dev->flags)&~(IFF_UP|IFF_RUNNING|IFF_PROMISC|IFF_ALLMULTI|IFF_VOLATILE)))
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);

	if ((flags^dev->gflags)&IFF_PROMISC) {
		int inc = (flags&IFF_PROMISC) ? +1 : -1;
		dev->gflags ^= IFF_PROMISC;
		dev_set_promiscuity(dev, inc);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	   is important. Some (broken) drivers set IFF_PROMISC, when
	   IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags^dev->gflags)&IFF_ALLMULTI) {
		int inc = (flags&IFF_ALLMULTI) ? +1 : -1;
		dev->gflags ^= IFF_ALLMULTI;
		dev_set_allmulti(dev, inc);
	}

	if (!ret && dev->change_flags)
		ret = dev->change_flags(dev, dev->flags);

	return ret;
}

#ifdef _HURD_

#define dev_ioctl 0

#else

/*
 *	Perform the SIOCxIFxxx calls.
 */

static int dev_ifsioc(struct ifreq *ifr, unsigned int cmd)
{
	struct device *dev;
	int err;

	if ((dev = dev_get(ifr->ifr_name)) == NULL)
		return -ENODEV;

	switch(cmd)
	{
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr->ifr_flags = (dev->flags&~(IFF_PROMISC|IFF_ALLMULTI))
				|(dev->gflags&(IFF_PROMISC|IFF_ALLMULTI));
			return 0;

		case SIOCSIFFLAGS:	/* Set interface flags */
			return dev_change_flags(dev, ifr->ifr_flags);

		case SIOCGIFMETRIC:	/* Get the metric on the interface (currently unused) */
			ifr->ifr_metric = 0;
			return 0;

		case SIOCSIFMETRIC:	/* Set the metric on the interface (currently unused) */
			return -EOPNOTSUPP;

		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr->ifr_mtu = dev->mtu;
			return 0;

		case SIOCSIFMTU:	/* Set the MTU of a device */
			if (ifr->ifr_mtu == dev->mtu)
				return 0;

			/*
			 *	MTU must be positive.
			 */

			if (ifr->ifr_mtu<=0)
				return -EINVAL;

			if (dev->change_mtu)
				err = dev->change_mtu(dev, ifr->ifr_mtu);
			else {
				dev->mtu = ifr->ifr_mtu;
				err = 0;
			}
			if (!err && dev->flags&IFF_UP)
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEMTU, dev);
			return err;

		case SIOCGIFHWADDR:
			memcpy(ifr->ifr_hwaddr.sa_data,dev->dev_addr, MAX_ADDR_LEN);
			ifr->ifr_hwaddr.sa_family=dev->type;
			return 0;

		case SIOCSIFHWADDR:
			if(dev->set_mac_address==NULL)
				return -EOPNOTSUPP;
			if(ifr->ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			err=dev->set_mac_address(dev,&ifr->ifr_hwaddr);
			if (!err)
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
			return err;

		case SIOCSIFHWBROADCAST:
			if(ifr->ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			memcpy(dev->broadcast, ifr->ifr_hwaddr.sa_data, MAX_ADDR_LEN);
			notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
			return 0;

		case SIOCGIFMAP:
			ifr->ifr_map.mem_start=dev->mem_start;
			ifr->ifr_map.mem_end=dev->mem_end;
			ifr->ifr_map.base_addr=dev->base_addr;
			ifr->ifr_map.irq=dev->irq;
			ifr->ifr_map.dma=dev->dma;
			ifr->ifr_map.port=dev->if_port;
			return 0;

		case SIOCSIFMAP:
			if (dev->set_config)
				return dev->set_config(dev,&ifr->ifr_map);
			return -EOPNOTSUPP;

		case SIOCADDMULTI:
			if(dev->set_multicast_list==NULL ||
			   ifr->ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_add(dev,ifr->ifr_hwaddr.sa_data, dev->addr_len, 1);
			return 0;

		case SIOCDELMULTI:
			if(dev->set_multicast_list==NULL ||
			   ifr->ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_delete(dev,ifr->ifr_hwaddr.sa_data,dev->addr_len, 1);
			return 0;

		case SIOCGIFINDEX:
			ifr->ifr_ifindex = dev->ifindex;
			return 0;

		case SIOCGIFTXQLEN:
			ifr->ifr_qlen = dev->tx_queue_len;
			return 0;

		case SIOCSIFTXQLEN:
			if(ifr->ifr_qlen<0)
				return -EINVAL;
			dev->tx_queue_len = ifr->ifr_qlen;
			return 0;

		case SIOCSIFNAME:
			if (dev->flags&IFF_UP)
				return -EBUSY;
			if (dev_get(ifr->ifr_newname))
				return -EEXIST;
			memcpy(dev->name, ifr->ifr_newname, IFNAMSIZ);
			dev->name[IFNAMSIZ-1] = 0;
			notifier_call_chain(&netdev_chain, NETDEV_CHANGENAME, dev);
			return 0;

		/*
		 *	Unknown or private ioctl
		 */

		default:
			if(cmd >= SIOCDEVPRIVATE &&
			   cmd <= SIOCDEVPRIVATE + 15) {
				if (dev->do_ioctl)
					return dev->do_ioctl(dev, ifr, cmd);
				return -EOPNOTSUPP;
			}

#ifdef CONFIG_NET_RADIO
			if(cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
				if (dev->do_ioctl)
					return dev->do_ioctl(dev, ifr, cmd);
				return -EOPNOTSUPP;
			}
#endif	/* CONFIG_NET_RADIO */

	}
	return -EINVAL;
}


/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	int ret;
	char *colon;

	/* One special case: SIOCGIFCONF takes ifconf argument
	   and requires shared lock, because it sleeps writing
	   to user space.
	 */

	if (cmd == SIOCGIFCONF) {
		rtnl_shlock();
		ret = dev_ifconf((char *) arg);
		rtnl_shunlock();
		return ret;
	}
	if (cmd == SIOCGIFNAME) {
		return dev_ifname((struct ifreq *)arg);
	}

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ-1] = 0;

	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;

	/*
	 *	See which interface the caller is talking about.
	 */

	switch(cmd)
	{
		/*
		 *	These ioctl calls:
		 *	- can be done by all.
		 *	- atomic and do not require locking.
		 *	- return a value
		 */

		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFHWADDR:
		case SIOCGIFSLAVE:
		case SIOCGIFMAP:
		case SIOCGIFINDEX:
		case SIOCGIFTXQLEN:
			dev_load(ifr.ifr_name);
			ret = dev_ifsioc(&ifr, cmd);
			if (!ret) {
				if (colon)
					*colon = ':';
				if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
			}
			return ret;

		/*
		 *	These ioctl calls:
		 *	- require superuser power.
		 *	- require strict serialization.
		 *	- do not return a value
		 */

		case SIOCSIFFLAGS:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMAP:
		case SIOCSIFHWADDR:
		case SIOCSIFSLAVE:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
		case SIOCSIFHWBROADCAST:
		case SIOCSIFTXQLEN:
		case SIOCSIFNAME:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			dev_load(ifr.ifr_name);
			rtnl_lock();
			ret = dev_ifsioc(&ifr, cmd);
			rtnl_unlock();
			return ret;

		case SIOCGIFMEM:
			/* Get the per device memory space. We can add this but currently
			   do not support it */
		case SIOCSIFMEM:
			/* Set the per device memory buffer space. Not applicable in our case */
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown or private ioctl.
		 */

		default:
			if (cmd >= SIOCDEVPRIVATE &&
			    cmd <= SIOCDEVPRIVATE + 15) {
				dev_load(ifr.ifr_name);
				rtnl_lock();
				ret = dev_ifsioc(&ifr, cmd);
				rtnl_unlock();
				if (!ret && copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
				return ret;
			}
#ifdef CONFIG_NET_RADIO
			if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
				dev_load(ifr.ifr_name);
				if (IW_IS_SET(cmd)) {
					if (!suser())
						return -EPERM;
					rtnl_lock();
				}
				ret = dev_ifsioc(&ifr, cmd);
				if (IW_IS_SET(cmd))
					rtnl_unlock();
				if (!ret && IW_IS_GET(cmd) &&
				    copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
				return ret;
			}
#endif	/* CONFIG_NET_RADIO */
			return -EINVAL;
	}
}

#endif

int dev_new_index(void)
{
	static int ifindex;
	for (;;) {
		if (++ifindex <= 0)
			ifindex=1;
		if (dev_get_by_index(ifindex) == NULL)
			return ifindex;
	}
}

static int dev_boot_phase = 1;


int register_netdevice(struct device *dev)
{
	struct device *d, **dp;

	if (dev_boot_phase) {
		/* This is NOT bug, but I am not sure, that all the
		   devices, initialized before netdev module is started
		   are sane.

		   Now they are chained to device boot list
		   and probed later. If a module is initialized
		   before netdev, but assumes that dev->init
		   is really called by register_netdev(), it will fail.

		   So that this message should be printed for a while.
		 */
		printk(KERN_INFO "early initialization of device %s is deferred\n", dev->name);

		/* Check for existence, and append to tail of chain */
		for (dp=&dev_base; (d=*dp) != NULL; dp=&d->next) {
			if (d == dev || strcmp(d->name, dev->name) == 0)
				return -EEXIST;
		}
		dev->next = NULL;
		*dp = dev;
		return 0;
	}

	dev->iflink = -1;

	/* Init, if this function is available */
	if (dev->init && dev->init(dev) != 0)
		return -EIO;

	/* Check for existence, and append to tail of chain */
	for (dp=&dev_base; (d=*dp) != NULL; dp=&d->next) {
		if (d == dev || strcmp(d->name, dev->name) == 0)
			return -EEXIST;
	}
	dev->next = NULL;
	dev_init_scheduler(dev);
	dev->ifindex = dev_new_index();
	if (dev->iflink == -1)
		dev->iflink = dev->ifindex;
	*dp = dev;

	/* Notify protocols, that a new device appeared. */
	notifier_call_chain(&netdev_chain, NETDEV_REGISTER, dev);

	return 0;
}

int unregister_netdevice(struct device *dev)
{
	struct device *d, **dp;

	if (dev_boot_phase == 0) {
		/* If device is running, close it.
		   It is very bad idea, really we should
		   complain loudly here, but random hackery
		   in linux/drivers/net likes it.
		 */
		if (dev->flags & IFF_UP)
			dev_close(dev);

#ifdef CONFIG_NET_FASTROUTE
		dev_clear_fastroute(dev);
#endif

		/* Shutdown queueing discipline. */
		dev_shutdown(dev);

		/* Notify protocols, that we are about to destroy
		   this device. They should clean all the things.
		 */
		notifier_call_chain(&netdev_chain, NETDEV_UNREGISTER, dev);

		/*
		 *	Flush the multicast chain
		 */
		dev_mc_discard(dev);

		/* To avoid pointers looking to nowhere,
		   we wait for end of critical section */
		dev_lock_wait();
	}

	/* And unlink it from device chain. */
	for (dp = &dev_base; (d=*dp) != NULL; dp=&d->next) {
		if (d == dev) {
			*dp = d->next;
			synchronize_bh();
			d->next = NULL;

			if (dev->destructor)
				dev->destructor(dev);
			return 0;
		}
	}
	return -ENODEV;
}


/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not
 *	present) and leaves us with a valid list of present and active devices.
 *
 */
extern int lance_init(void);
extern int bpq_init(void);
extern int scc_init(void);
extern void sdla_setup(void);
extern void sdla_c_setup(void);
extern void dlci_setup(void);
extern int dmascc_init(void);
extern int sm_init(void);

extern int baycom_ser_fdx_init(void);
extern int baycom_ser_hdx_init(void);
extern int baycom_par_init(void);

extern int lapbeth_init(void);
extern int comx_init(void);
extern void arcnet_init(void);
extern void ip_auto_config(void);
#ifdef CONFIG_8xx
extern int cpm_enet_init(void);
#endif /* CONFIG_8xx */

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_dev = {
	PROC_NET_DEV, 3, "dev",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	dev_get_info
};
#endif

#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_wireless = {
	PROC_NET_WIRELESS, 8, "wireless",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	dev_get_wireless_info
};
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */

__initfunc(int net_dev_init(void))
{
	struct device *dev, **dp;

#ifdef CONFIG_NET_SCHED
	pktsched_init();
#endif

	/*
	 *	Initialise the packet receive queue.
	 */

	skb_queue_head_init(&backlog);

	/*
	 *	The bridge has to be up before the devices
	 */

#ifdef CONFIG_BRIDGE
	br_init();
#endif

	/*
	 * This is Very Ugly(tm).
	 *
	 * Some devices want to be initialized early..
	 */

#if defined(CONFIG_SCC)
	scc_init();
#endif
#if defined(CONFIG_DMASCC)
	dmascc_init();
#endif
#if defined(CONFIG_BPQETHER)
	bpq_init();
#endif
#if defined(CONFIG_DLCI)
	dlci_setup();
#endif
#if defined(CONFIG_SDLA)
	sdla_c_setup();
#endif
#if defined(CONFIG_BAYCOM_PAR)
	baycom_par_init();
#endif
#if defined(CONFIG_BAYCOM_SER_FDX)
	baycom_ser_fdx_init();
#endif
#if defined(CONFIG_BAYCOM_SER_HDX)
	baycom_ser_hdx_init();
#endif
#if defined(CONFIG_SOUNDMODEM)
	sm_init();
#endif
#if defined(CONFIG_LAPBETHER)
	lapbeth_init();
#endif
#if defined(CONFIG_PLIP)
	plip_init();
#endif
#if defined(CONFIG_ARCNET)
	arcnet_init();
#endif
#if defined(CONFIG_8xx)
        cpm_enet_init();
#endif
#if defined(CONFIG_COMX)
	comx_init();
#endif
	/*
	 *	SLHC if present needs attaching so other people see it
	 *	even if not opened.
	 */

#ifdef CONFIG_INET
#if (defined(CONFIG_SLIP) && defined(CONFIG_SLIP_COMPRESSED)) \
	 || defined(CONFIG_PPP) \
    || (defined(CONFIG_ISDN) && defined(CONFIG_ISDN_PPP))
	slhc_install();
#endif
#endif

#ifdef CONFIG_NET_PROFILE
	net_profile_init();
	NET_PROFILE_REGISTER(dev_queue_xmit);
	NET_PROFILE_REGISTER(net_bh);
#if 0
	NET_PROFILE_REGISTER(net_bh_skb);
#endif
#endif
	/*
	 *	Add the devices.
	 *	If the call to dev->init fails, the dev is removed
	 *	from the chain disconnecting the device until the
	 *	next reboot.
	 */

	dp = &dev_base;
	while ((dev = *dp) != NULL)
	{
		dev->iflink = -1;
		if (dev->init && dev->init(dev))
		{
			/*
			 *	It failed to come up. Unhook it.
			 */
			*dp = dev->next;
			synchronize_bh();
		}
		else
		{
			dp = &dev->next;
			dev->ifindex = dev_new_index();
			if (dev->iflink == -1)
				dev->iflink = dev->ifindex;
			dev_init_scheduler(dev);
		}
	}

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_dev);
	{
		struct proc_dir_entry *ent = create_proc_entry("net/dev_stat", 0, 0);
		ent->read_proc = dev_proc_stats;
	}
#endif

#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_wireless);
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */

	init_bh(NET_BH, net_bh);

	dev_boot_phase = 0;

	dev_mcast_init();

#ifdef CONFIG_BRIDGE
	/*
	 * Register any statically linked ethernet devices with the bridge
	 */
	br_spacedevice_register();
#endif

#ifdef CONFIG_IP_PNP
	ip_auto_config();
#endif

	return 0;
}
