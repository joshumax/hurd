/*
 *	Implements an IPX socket layer (badly - but I'm working on it).
 *
 *	This code is derived from work by
 *		Ross Biro	: 	Writing the original IP stack
 *		Fred Van Kempen :	Tidying up the TCP/IP
 *
 *	Many thanks go to Keith Baker, Institute For Industrial Information
 *	Technology Ltd, Swansea University for allowing me to work on this
 *	in my own time even though it was in some ways related to commercial
 *	work I am currently employed to do there.
 *
 *	All the material in this file is subject to the Gnu license version 2.
 *	Neither Alan Cox nor the Swansea University Computer Society admit liability
 *	nor provide warranty for any of this software. This material is provided 
 *	as is and at no charge.		
 *
 *	Revision 0.21:	Uses the new generic socket option code.
 *	Revision 0.22:	Gcc clean ups and drop out device registration. Use the
 *			new multi-protocol edition of hard_header 
 *	Revision 0.23:  IPX /proc by Mark Evans.
 *     			Adding a route will overwrite any existing route to the same
 *			network.
 *	Revision 0.24:	Supports new /proc with no 4K limit
 *	Revision 0.25:	Add ephemeral sockets, passive local network 
 *			identification, support for local net 0 and
 *			multiple datalinks <Greg Page>
 *	Revision 0.26:  Device drop kills IPX routes via it. (needed for modules)
 *	Revision 0.27:  Autobind <Mark Evans>
 *	Revision 0.28:  Small fix for multiple local networks <Thomas Winder>
 *	Revision 0.29:  Assorted major errors removed <Mark Evans>
 *			Small correction to promisc mode error fix <Alan Cox>
 *			Asynchronous I/O support.
 *			Changed to use notifiers and the newer packet_type stuff.
 *			Assorted major fixes <Alejandro Liu>
 *
 * 	Portions Copyright (c) 1995 Caldera, Inc. <greg@caldera.com>
 *	Neither Greg Page nor Caldera, Inc. admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.		
 */
  
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/ipx.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "sock.h"
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/termios.h>	/* For TIOCOUTQ/INQ */
#include <linux/interrupt.h>
#include "p8022.h"
#include "psnap.h"

#ifdef CONFIG_IPX
/* Configuration Variables */
static unsigned char	ipxcfg_max_hops = 16;
static char	ipxcfg_auto_select_primary = 0;
static char	ipxcfg_auto_create_interfaces = 0;

/* Global Variables */
static struct datalink_proto	*p8022_datalink = NULL;
static struct datalink_proto	*pEII_datalink = NULL;
static struct datalink_proto	*p8023_datalink = NULL;
static struct datalink_proto	*pSNAP_datalink = NULL;

static ipx_interface	*ipx_interfaces = NULL;
static ipx_route 	*ipx_routes = NULL;
static ipx_interface	*ipx_internal_net = NULL;
static ipx_interface	*ipx_primary_net = NULL;

static int
ipxcfg_set_auto_create(char val)
{
	ipxcfg_auto_create_interfaces = val;
	return 0;
}
		
static int
ipxcfg_set_auto_select(char val)
{
	ipxcfg_auto_select_primary = val;
	if (val && (ipx_primary_net == NULL))
		ipx_primary_net = ipx_interfaces;
	return 0;
}

static int
ipxcfg_get_config_data(ipx_config_data *arg)
{
	ipx_config_data	vals;
	
	vals.ipxcfg_auto_create_interfaces = ipxcfg_auto_create_interfaces;
	vals.ipxcfg_auto_select_primary = ipxcfg_auto_select_primary;
	memcpy_tofs(arg, &vals, sizeof(vals));
	return 0;
}


/***********************************************************************************************************************\
*															*
*						Handlers for the socket list.						*
*															*
\***********************************************************************************************************************/

/*
 *	Note: Sockets may not be removed _during_ an interrupt or inet_bh
 *	handler using this technique. They can be added although we do not
 *	use this facility.
 */
 
static void 
ipx_remove_socket(ipx_socket *sk)
{
	ipx_socket	*s;
	ipx_interface	*intrfc;
	unsigned long	flags;

	save_flags(flags);
	cli();
	
	/* Determine interface with which socket is associated */
	intrfc = sk->ipx_intrfc;
	if (intrfc == NULL) {
		restore_flags(flags);
		return;
	}

	s=intrfc->if_sklist;
	if(s==sk) {
		intrfc->if_sklist=s->next;
		restore_flags(flags);
		return;
	} 

	while(s && s->next) {
		if(s->next==sk) {
			s->next=sk->next;
			restore_flags(flags);
			return;
		}
		s=s->next;
	}
	restore_flags(flags);
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
 
static void 
ipx_destroy_socket(ipx_socket *sk)
{
	struct sk_buff	*skb;

	ipx_remove_socket(sk);
	while((skb=skb_dequeue(&sk->receive_queue))!=NULL) {
		kfree_skb(skb,FREE_READ);
	}
	
	kfree_s(sk,sizeof(*sk));
}
	
/* The following code is used to support IPX Interfaces (IPXITF).  An
 * IPX interface is defined by a physical device and a frame type.
 */

static ipx_route * ipxrtr_lookup(unsigned long);
 
static void
ipxitf_clear_primary_net(void)
{
	if (ipxcfg_auto_select_primary && (ipx_interfaces != NULL))
		ipx_primary_net = ipx_interfaces;
	else
		ipx_primary_net = NULL;
}

static ipx_interface *
ipxitf_find_using_phys(struct device *dev, unsigned short datalink)
{
	ipx_interface	*i;

	for (i=ipx_interfaces; 
		i && ((i->if_dev!=dev) || (i->if_dlink_type!=datalink)); 
		i=i->if_next)
		;
	return i;
}

static ipx_interface *
ipxitf_find_using_net(unsigned long net)
{
	ipx_interface	*i;

	if (net == 0L)
		return ipx_primary_net;

	for (i=ipx_interfaces; i && (i->if_netnum!=net); i=i->if_next)
		;

	return i;
}

/* Sockets are bound to a particular IPX interface. */
static void
ipxitf_insert_socket(ipx_interface *intrfc, ipx_socket *sk)
{
	ipx_socket	*s;

	sk->ipx_intrfc = intrfc;
	sk->next = NULL;
	if (intrfc->if_sklist == NULL) {
		intrfc->if_sklist = sk;
	} else {
		for (s = intrfc->if_sklist; s->next != NULL; s = s->next)
			;
		s->next = sk;
	}
}

static ipx_socket *
ipxitf_find_socket(ipx_interface *intrfc, unsigned short port)
{
	ipx_socket	*s;

	for (s=intrfc->if_sklist; 
		(s != NULL) && (s->ipx_port != port); 
		s=s->next)
		;

	return s;
}

static void ipxrtr_del_routes(ipx_interface *);

static void
ipxitf_down(ipx_interface *intrfc)
{
	ipx_interface	*i;
	ipx_socket	*s, *t;

	/* Delete all routes associated with this interface */
	ipxrtr_del_routes(intrfc);

	/* error sockets */
	for (s = intrfc->if_sklist; s != NULL; ) {
		s->err = ENOLINK;
		s->error_report(s);
		s->ipx_intrfc = NULL;
		s->ipx_port = 0;
		s->zapped=1;	/* Indicates it is no longer bound */
		t = s;
		s = s->next;
		t->next = NULL;
	}
	intrfc->if_sklist = NULL;

	/* remove this interface from list */
	if (intrfc == ipx_interfaces) {
		ipx_interfaces = intrfc->if_next;
	} else {
		for (i = ipx_interfaces; 
			(i != NULL) && (i->if_next != intrfc);
			i = i->if_next)
			;
		if ((i != NULL) && (i->if_next == intrfc)) 
			i->if_next = intrfc->if_next;
	}

	/* remove this interface from *special* networks */
	if (intrfc == ipx_primary_net)
		ipxitf_clear_primary_net();
	if (intrfc == ipx_internal_net)
		ipx_internal_net = NULL;

	kfree_s(intrfc, sizeof(*intrfc));
}

static int 
ipxitf_device_event(unsigned long event, void *ptr)
{
	struct device *dev = ptr;
	ipx_interface *i, *tmp;

	if(event!=NETDEV_DOWN)
		return NOTIFY_DONE;

	for (i = ipx_interfaces; i != NULL; ) {
	
		tmp = i->if_next;
		if (i->if_dev == dev) 
			ipxitf_down(i);
		i = tmp;

	}

	return NOTIFY_DONE;
}

static int
ipxitf_def_skb_handler(struct sock *sock, struct sk_buff *skb)
{
	int	retval;

	if((retval = sock_queue_rcv_skb(sock, skb))<0) {
		/*
	 	 *	We do a FREE_WRITE here because this indicates how
	 	 *	to treat the socket with which the packet is 
	 	 *	associated.  If this packet is associated with a
	 	 *	socket at all, it must be the originator of the 
	 	 *	packet.   Incoming packets will have no socket 
		 *	associated with them at this point.
	 	 */
		kfree_skb(skb,FREE_WRITE);
	}
	return retval;
}

static int
ipxitf_demux_socket(ipx_interface *intrfc, struct sk_buff *skb, int copy) 
{
	ipx_packet	*ipx = (ipx_packet *)(skb->h.raw);
	ipx_socket	*sock1 = NULL, *sock2 = NULL;
	struct sk_buff	*skb1 = NULL, *skb2 = NULL;
	int		ipx_offset;

	sock1 = ipxitf_find_socket(intrfc, ipx->ipx_dest.sock);

	/*
	 *	We need to check if there is a primary net and if
	 *	this is addressed to one of the *SPECIAL* sockets because
	 *	these need to be propagated to the primary net.
	 *	The *SPECIAL* socket list contains: 0x452(SAP), 0x453(RIP) and
	 *	0x456(Diagnostic).
	 */
	if (ipx_primary_net && (intrfc != ipx_primary_net)) {
		switch (ntohs(ipx->ipx_dest.sock)) {
		case 0x452:
		case 0x453:
		case 0x456:
			/*
			 *	The appropriate thing to do here is to
			 * 	dup the packet and route to the primary net
			 *	interface via ipxitf_send; however, we'll cheat
			 *	and just demux it here.
			 */
			sock2 = ipxitf_find_socket(ipx_primary_net, 
					ipx->ipx_dest.sock);
			break;
		default:
			break;
		}
	}

	/* if there is nothing to do, return */
	if ((sock1 == NULL) && (sock2 == NULL)) {
		if (!copy) 
			kfree_skb(skb,FREE_WRITE);
		return 0;
	}

	ipx_offset = (char *)(skb->h.raw) - (char *)(skb->data);

	/* This next segment of code is a little awkward, but it sets it up
	 * so that the appropriate number of copies of the SKB are made and 
	 * that skb1 and skb2 point to it (them) so that it (they) can be 
	 * demuxed to sock1 and/or sock2.  If we are unable to make enough
	 * copies, we do as much as is possible.
	 */
	if (copy) {
		skb1 = skb_clone(skb, GFP_ATOMIC);
		if (skb1 != NULL) {
			skb1->h.raw = (unsigned char *)&(skb1->data[ipx_offset]);
			skb1->arp = skb1->free = 1;
		}
	} else {
		skb1 = skb;
	}
	
	if (skb1 == NULL) return -ENOMEM; 

	/* Do we need 2 SKBs? */
	if (sock1 && sock2) {
		skb2 = skb_clone(skb1, GFP_ATOMIC);
		if (skb2 != NULL) {
			skb2->h.raw = (unsigned char *)&(skb2->data[ipx_offset]);
			skb2->arp = skb2->free = 1;
		}
	} else {
		skb2 = skb1;
	}
		
	if (sock1) {
		(void) ipxitf_def_skb_handler(sock1, skb1);
	}

	if (skb2 == NULL) return -ENOMEM;

	if (sock2) {
		(void) ipxitf_def_skb_handler(sock2, skb2);
	}

	return 0;
}

static struct sk_buff *
ipxitf_adjust_skbuff(ipx_interface *intrfc, struct sk_buff *skb)
{
	struct sk_buff	*skb2;
	int	in_offset = skb->h.raw - skb->data;
	int	out_offset = intrfc->if_ipx_offset;
	char	*oldraw;
	int	len;

	/* Hopefully, most cases */
	if (in_offset == out_offset) {
		skb->len += out_offset;
		skb->arp = skb->free = 1;
		return skb;
	}

	/* Existing SKB will work, just need to move things around a little */
	if (in_offset > out_offset) {
		oldraw = skb->h.raw;
		skb->h.raw = &(skb->data[out_offset]);
		memmove(skb->h.raw, oldraw, skb->len);
		skb->len += out_offset;
		skb->arp = skb->free = 1;
		return skb;
	}

	/* Need new SKB */
	len = skb->len + out_offset;
	skb2 = alloc_skb(len, GFP_ATOMIC);
	if (skb2 != NULL) {
		skb2->h.raw = &(skb2->data[out_offset]);
		skb2->len = len;
		skb2->free=1;
		skb2->arp=1;
		memcpy(skb2->h.raw, skb->h.raw, skb->len);
	}
	kfree_skb(skb, FREE_WRITE);
	return skb2;
}

static int
ipxitf_send(ipx_interface *intrfc, struct sk_buff *skb, char *node)
{
	ipx_packet	*ipx = (ipx_packet *)(skb->h.raw);
	struct device	*dev = intrfc->if_dev;
	struct datalink_proto	*dl = intrfc->if_dlink;
	char		dest_node[IPX_NODE_LEN];
	int		send_to_wire = 1;
	int		addr_len;
	
	/* We need to know how many skbuffs it will take to send out this
	 * packet to avoid unnecessary copies.
	 */
	if ((dl == NULL) || (dev == NULL) || (dev->flags & IFF_LOOPBACK)) 
		send_to_wire = 0;

	/* See if this should be demuxed to sockets on this interface */
	if (ipx->ipx_dest.net == intrfc->if_netnum) {
		if (memcmp(intrfc->if_node, node, IPX_NODE_LEN) == 0) 
			return ipxitf_demux_socket(intrfc, skb, 0);
		if (memcmp(ipx_broadcast_node, node, IPX_NODE_LEN) == 0) {
			ipxitf_demux_socket(intrfc, skb, send_to_wire);
			if (!send_to_wire) return 0;
		}
	}

	/* if the originating net is not equal to our net; this is routed */
	if (ipx->ipx_source.net != intrfc->if_netnum) {
		if (++(ipx->ipx_tctrl) > ipxcfg_max_hops) 
			send_to_wire = 0;
	}

	if (!send_to_wire) {
		/*
		 *	We do a FREE_WRITE here because this indicates how
		 *	to treat the socket with which the packet is 
	 	 *	associated.  If this packet is associated with a
		 *	socket at all, it must be the originator of the 
		 *	packet.   Routed packets will have no socket associated
		 *	with them.
		 */
		kfree_skb(skb,FREE_WRITE);
		return 0;
	}

	/* determine the appropriate hardware address */
	addr_len = dev->addr_len;
	if (memcmp(ipx_broadcast_node, node, IPX_NODE_LEN) == 0) {
		memcpy(dest_node, dev->broadcast, addr_len);
	} else {
		memcpy(dest_node, &(node[IPX_NODE_LEN-addr_len]), addr_len);
	}

	/* make any compensation for differing physical/data link size */
	skb = ipxitf_adjust_skbuff(intrfc, skb);
	if (skb == NULL) return 0;

	/* set up data link and physical headers */
	skb->dev = dev;
	dl->datalink_header(dl, skb, dest_node);

	if (skb->sk != NULL) {
		/* This is an outbound packet from this host.  We need to 
		 * increment the write count.
		 */
		skb->sk->wmem_alloc += skb->mem_len;
	}

	/* Send it out */
	dev_queue_xmit(skb, dev, SOPRI_NORMAL);
	return 0;
}

static int
ipxrtr_add_route(unsigned long, ipx_interface *, unsigned char *);

static int
ipxitf_add_local_route(ipx_interface *intrfc)
{
	return ipxrtr_add_route(intrfc->if_netnum, intrfc, NULL);
}

static char * ipx_frame_name(unsigned short);
static char * ipx_device_name(ipx_interface *);
static int ipxrtr_route_skb(struct sk_buff *);

static int 
ipxitf_rcv(ipx_interface *intrfc, struct sk_buff *skb)
{
	ipx_packet	*ipx = (ipx_packet *) (skb->h.raw);
	ipx_interface	*i;

	/* See if we should update our network number */
	if ((intrfc->if_netnum == 0L) && 
		(ipx->ipx_source.net == ipx->ipx_dest.net) &&
		(ipx->ipx_source.net != 0L)) {
		/* NB: NetWare servers lie about their hop count so we
		 * dropped the test based on it.  This is the best way
		 * to determine this is a 0 hop count packet.
		 */
		if ((i=ipxitf_find_using_net(ipx->ipx_source.net))==NULL) {
			intrfc->if_netnum = ipx->ipx_source.net;
			(void) ipxitf_add_local_route(intrfc);
		} else {
			printk("IPX: Network number collision %lx\n\t%s %s and %s %s\n",
				htonl(ipx->ipx_source.net), 
				ipx_device_name(i),
				ipx_frame_name(i->if_dlink_type),
				ipx_device_name(intrfc),
				ipx_frame_name(intrfc->if_dlink_type));
		}
	}

	if (ipx->ipx_dest.net == 0L)
		ipx->ipx_dest.net = intrfc->if_netnum;
	if (ipx->ipx_source.net == 0L)
		ipx->ipx_source.net = intrfc->if_netnum;

	if (intrfc->if_netnum != ipx->ipx_dest.net) {
		/* We only route point-to-point packets. */
		if ((skb->pkt_type != PACKET_BROADCAST) &&
			(skb->pkt_type != PACKET_MULTICAST))
			return ipxrtr_route_skb(skb);
		
		kfree_skb(skb,FREE_READ);
		return 0;
	}

	/* see if we should keep it */
	if ((memcmp(ipx_broadcast_node, ipx->ipx_dest.node, IPX_NODE_LEN) == 0) 
		|| (memcmp(intrfc->if_node, ipx->ipx_dest.node, IPX_NODE_LEN) == 0)) {
		return ipxitf_demux_socket(intrfc, skb, 0);
	}

	/* we couldn't pawn it off so unload it */
	kfree_skb(skb,FREE_READ);
	return 0;
}

static void
ipxitf_insert(ipx_interface *intrfc)
{
	ipx_interface	*i;

	intrfc->if_next = NULL;
	if (ipx_interfaces == NULL) {
		ipx_interfaces = intrfc;
	} else {
		for (i = ipx_interfaces; i->if_next != NULL; i = i->if_next)
			;
		i->if_next = intrfc;
	}

	if (ipxcfg_auto_select_primary && (ipx_primary_net == NULL))
		ipx_primary_net = intrfc;
}

static int 
ipxitf_create_internal(ipx_interface_definition *idef)
{
	ipx_interface	*intrfc;

	/* Only one primary network allowed */
	if (ipx_primary_net != NULL) return -EEXIST;

	/* Must have a valid network number */
	if (idef->ipx_network == 0L) return -EADDRNOTAVAIL;
	if (ipxitf_find_using_net(idef->ipx_network) != NULL)
		return -EADDRINUSE;

	intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
	if (intrfc==NULL)
		return -EAGAIN;
	intrfc->if_dev=NULL;
	intrfc->if_netnum=idef->ipx_network;
	intrfc->if_dlink_type = 0;
	intrfc->if_dlink = NULL;
	intrfc->if_sklist = NULL;
	intrfc->if_internal = 1;
	intrfc->if_ipx_offset = 0;
	intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
	memcpy((char *)&(intrfc->if_node), idef->ipx_node, IPX_NODE_LEN);
	ipx_internal_net = intrfc;
	ipx_primary_net = intrfc;
	ipxitf_insert(intrfc);
	return ipxitf_add_local_route(intrfc);
}

static int
ipx_map_frame_type(unsigned char type)
{
	switch (type) {
	case IPX_FRAME_ETHERII: return htons(ETH_P_IPX);
	case IPX_FRAME_8022: return htons(ETH_P_802_2);
	case IPX_FRAME_SNAP: return htons(ETH_P_SNAP);
	case IPX_FRAME_8023: return htons(ETH_P_802_3);
	}
	return 0;
}

static int 
ipxitf_create(ipx_interface_definition *idef)
{
	struct device	*dev;
	unsigned short	dlink_type = 0;
	struct datalink_proto	*datalink = NULL;
	ipx_interface	*intrfc;

	if (idef->ipx_special == IPX_INTERNAL) 
		return ipxitf_create_internal(idef);

	if ((idef->ipx_special == IPX_PRIMARY) && (ipx_primary_net != NULL))
		return -EEXIST;

	if ((idef->ipx_network != 0L) &&
		(ipxitf_find_using_net(idef->ipx_network) != NULL))
		return -EADDRINUSE;

	switch (idef->ipx_dlink_type) {
	case IPX_FRAME_ETHERII: 
		dlink_type = htons(ETH_P_IPX);
		datalink = pEII_datalink;
		break;
	case IPX_FRAME_8022:
		dlink_type = htons(ETH_P_802_2);
		datalink = p8022_datalink;
		break;
	case IPX_FRAME_SNAP:
		dlink_type = htons(ETH_P_SNAP);
		datalink = pSNAP_datalink;
		break;
	case IPX_FRAME_8023:
		dlink_type = htons(ETH_P_802_3);
		datalink = p8023_datalink;
		break;
	case IPX_FRAME_NONE:
	default:
		break;
	}

	if (datalink == NULL) 
		return -EPROTONOSUPPORT;

	dev=dev_get(idef->ipx_device);
	if (dev==NULL) 
		return -ENODEV;

	if (!(dev->flags & IFF_UP))
		return -ENETDOWN;

	/* Check addresses are suitable */
	if(dev->addr_len>IPX_NODE_LEN)
		return -EINVAL;

	if ((intrfc = ipxitf_find_using_phys(dev, dlink_type)) == NULL) {

		/* Ok now create */
		intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
		if (intrfc==NULL)
			return -EAGAIN;
		intrfc->if_dev=dev;
		intrfc->if_netnum=idef->ipx_network;
		intrfc->if_dlink_type = dlink_type;
		intrfc->if_dlink = datalink;
		intrfc->if_sklist = NULL;
		intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
		/* Setup primary if necessary */
		if ((idef->ipx_special == IPX_PRIMARY)) 
			ipx_primary_net = intrfc;
		intrfc->if_internal = 0;
		intrfc->if_ipx_offset = dev->hard_header_len + datalink->header_length;
		memset(intrfc->if_node, 0, IPX_NODE_LEN);
		memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]), dev->dev_addr, dev->addr_len);

		ipxitf_insert(intrfc);
	}

	/* If the network number is known, add a route */
	if (intrfc->if_netnum == 0L) 
		return 0;

	return ipxitf_add_local_route(intrfc);
}

static int 
ipxitf_delete(ipx_interface_definition *idef)
{
	struct device	*dev = NULL;
	unsigned short	dlink_type = 0;
	ipx_interface	*intrfc;

	if (idef->ipx_special == IPX_INTERNAL) {
		if (ipx_internal_net != NULL) {
			ipxitf_down(ipx_internal_net);
			return 0;
		}
		return -ENOENT;
	}

	dlink_type = ipx_map_frame_type(idef->ipx_dlink_type);
	if (dlink_type == 0)
		return -EPROTONOSUPPORT;

	dev=dev_get(idef->ipx_device);
	if(dev==NULL) return -ENODEV;

	intrfc = ipxitf_find_using_phys(dev, dlink_type);
	if (intrfc != NULL) {
		ipxitf_down(intrfc);
		return 0;
	}
	return -EINVAL;
}

static ipx_interface *
ipxitf_auto_create(struct device *dev, unsigned short dlink_type)
{
	struct datalink_proto *datalink = NULL;
	ipx_interface	*intrfc;

	switch (htons(dlink_type)) {
	case ETH_P_IPX: datalink = pEII_datalink; break;
	case ETH_P_802_2: datalink = p8022_datalink; break;
	case ETH_P_SNAP: datalink = pSNAP_datalink; break;
	case ETH_P_802_3: datalink = p8023_datalink; break;
	default: return NULL;
	}
	
	if (dev == NULL)
		return NULL;

	/* Check addresses are suitable */
	if(dev->addr_len>IPX_NODE_LEN) return NULL;

	intrfc=(ipx_interface *)kmalloc(sizeof(ipx_interface),GFP_ATOMIC);
	if (intrfc!=NULL) {
		intrfc->if_dev=dev;
		intrfc->if_netnum=0L;
		intrfc->if_dlink_type = dlink_type;
		intrfc->if_dlink = datalink;
		intrfc->if_sklist = NULL;
		intrfc->if_internal = 0;
		intrfc->if_sknum = IPX_MIN_EPHEMERAL_SOCKET;
		intrfc->if_ipx_offset = dev->hard_header_len + 
			datalink->header_length;
		memset(intrfc->if_node, 0, IPX_NODE_LEN);
		memcpy((char *)&(intrfc->if_node[IPX_NODE_LEN-dev->addr_len]), 
			dev->dev_addr, dev->addr_len);
		ipxitf_insert(intrfc);
	}

	return intrfc;
}

static int 
ipxitf_ioctl(unsigned int cmd, void *arg)
{
	int err;
	switch(cmd)
	{
		case SIOCSIFADDR:
		{
			struct ifreq ifr;
			struct sockaddr_ipx *sipx;
			ipx_interface_definition f;
			err=verify_area(VERIFY_READ,arg,sizeof(ifr));
			if(err)
				return err;
			memcpy_fromfs(&ifr,arg,sizeof(ifr));
			sipx=(struct sockaddr_ipx *)&ifr.ifr_addr;
			if(sipx->sipx_family!=AF_IPX)
				return -EINVAL;
			f.ipx_network=sipx->sipx_network;
			memcpy(f.ipx_device, ifr.ifr_name, sizeof(f.ipx_device));
			memcpy(f.ipx_node, sipx->sipx_node, IPX_NODE_LEN);
			f.ipx_dlink_type=sipx->sipx_type;
			f.ipx_special=sipx->sipx_special;
			if(sipx->sipx_action==IPX_DLTITF)
				return ipxitf_delete(&f);
			else
				return ipxitf_create(&f);
		}
		case SIOCGIFADDR:
		{
			struct ifreq ifr;
			struct sockaddr_ipx *sipx;
			ipx_interface *ipxif;
			struct device *dev;
			err=verify_area(VERIFY_WRITE,arg,sizeof(ifr));
			if(err)
				return err;
			memcpy_fromfs(&ifr,arg,sizeof(ifr));
			sipx=(struct sockaddr_ipx *)&ifr.ifr_addr;
			dev=dev_get(ifr.ifr_name);
			if(!dev)
				return -ENODEV;
			ipxif=ipxitf_find_using_phys(dev, ipx_map_frame_type(sipx->sipx_type));
			if(ipxif==NULL)
				return -EADDRNOTAVAIL;
			sipx->sipx_network=ipxif->if_netnum;
			memcpy(sipx->sipx_node, ipxif->if_node, sizeof(sipx->sipx_node));
			memcpy_tofs(arg,&ifr,sizeof(ifr));
			return 0;
		}
		case SIOCAIPXITFCRT:
			err=verify_area(VERIFY_READ,arg,sizeof(char));
			if(err)
				return err;
			return ipxcfg_set_auto_create(get_fs_byte(arg));
		case SIOCAIPXPRISLT:
			err=verify_area(VERIFY_READ,arg,sizeof(char));
			if(err)
				return err;
			return ipxcfg_set_auto_select(get_fs_byte(arg));
		default:
			return -EINVAL;
	}
}

/*******************************************************************************************************************\
*													            *
*	            			Routing tables for the IPX socket layer				            *
*														    *
\*******************************************************************************************************************/

static ipx_route *
ipxrtr_lookup(unsigned long net)
{
	ipx_route *r;

	for (r=ipx_routes; (r!=NULL) && (r->ir_net!=net); r=r->ir_next)
		;

	return r;
}

static int
ipxrtr_add_route(unsigned long network, ipx_interface *intrfc, unsigned char *node)
{
	ipx_route	*rt;

	/* Get a route structure; either existing or create */
	rt = ipxrtr_lookup(network);
	if (rt==NULL) {
		rt=(ipx_route *)kmalloc(sizeof(ipx_route),GFP_ATOMIC);
		if(rt==NULL)
			return -EAGAIN;
		rt->ir_next=ipx_routes;
		ipx_routes=rt;
	}

	rt->ir_net = network;
	rt->ir_intrfc = intrfc;
	if (node == NULL) {
		memset(rt->ir_router_node, '\0', IPX_NODE_LEN);
		rt->ir_routed = 0;
	} else {
		memcpy(rt->ir_router_node, node, IPX_NODE_LEN);
		rt->ir_routed=1;
	}
	return 0;
}

static void
ipxrtr_del_routes(ipx_interface *intrfc)
{
	ipx_route	**r, *tmp;

	for (r = &ipx_routes; (tmp = *r) != NULL; ) {
		if (tmp->ir_intrfc == intrfc) {
			*r = tmp->ir_next;
			kfree_s(tmp, sizeof(ipx_route));
		} else {
			r = &(tmp->ir_next);
		}
	}
}

static int 
ipxrtr_create(ipx_route_definition *rd)
{
	ipx_interface *intrfc;

	/* Find the appropriate interface */
	intrfc = ipxitf_find_using_net(rd->ipx_router_network);
	if (intrfc == NULL)
		return -ENETUNREACH;

	return ipxrtr_add_route(rd->ipx_network, intrfc, rd->ipx_router_node);
}


static int 
ipxrtr_delete(long net)
{
	ipx_route	**r;
	ipx_route	*tmp;

	for (r = &ipx_routes; (tmp = *r) != NULL; ) {
		if (tmp->ir_net == net) {
			if (!(tmp->ir_routed)) {
				/* Directly connected; can't lose route */
				return -EPERM;
			}
			*r = tmp->ir_next;
			kfree_s(tmp, sizeof(ipx_route));
			return 0;
		} 
		r = &(tmp->ir_next);
	}

	return -ENOENT;
}

static int
ipxrtr_route_packet(ipx_socket *sk, struct sockaddr_ipx *usipx, void *ubuf, int len)
{
	struct sk_buff *skb;
	ipx_interface *intrfc;
	ipx_packet *ipx;
	int size;
	int ipx_offset;
	ipx_route *rt = NULL;

	/* Find the appropriate interface on which to send packet */
	if ((usipx->sipx_network == 0L) && (ipx_primary_net != NULL)) {
		usipx->sipx_network = ipx_primary_net->if_netnum;
		intrfc = ipx_primary_net;
	} else {
		rt = ipxrtr_lookup(usipx->sipx_network);
		if (rt==NULL) {
			return -ENETUNREACH;
		}
		intrfc = rt->ir_intrfc;
	}
	
	ipx_offset = intrfc->if_ipx_offset;
	size=sizeof(ipx_packet)+len;
	size += ipx_offset;

	if(size+sk->wmem_alloc>sk->sndbuf) return -EAGAIN;
		
	skb=alloc_skb(size,GFP_KERNEL);
	if(skb==NULL) return -ENOMEM;
		
	skb->sk=sk;
	skb->len=size;
	skb->free=1;
	skb->arp=1;

	/* Fill in IPX header */
	ipx=(ipx_packet *)&(skb->data[ipx_offset]);
	ipx->ipx_checksum=0xFFFF;
	ipx->ipx_pktsize=htons(len+sizeof(ipx_packet));
	ipx->ipx_tctrl=0;
	ipx->ipx_type=usipx->sipx_type;
	skb->h.raw = (unsigned char *)ipx;

	ipx->ipx_source.net = sk->ipx_intrfc->if_netnum;
	memcpy(ipx->ipx_source.node, sk->ipx_intrfc->if_node, IPX_NODE_LEN);
	ipx->ipx_source.sock = sk->ipx_port;
	ipx->ipx_dest.net=usipx->sipx_network;
	memcpy(ipx->ipx_dest.node,usipx->sipx_node,IPX_NODE_LEN);
	ipx->ipx_dest.sock=usipx->sipx_port;

	memcpy_fromfs((char *)(ipx+1),ubuf,len);
	return ipxitf_send(intrfc, skb, (rt && rt->ir_routed) ? 
				rt->ir_router_node : ipx->ipx_dest.node);
}
	
static int
ipxrtr_route_skb(struct sk_buff *skb)
{
	ipx_packet	*ipx = (ipx_packet *) (skb->h.raw);
	ipx_route	*r;
	ipx_interface	*i;

	r = ipxrtr_lookup(ipx->ipx_dest.net);
	if (r == NULL) {
		/* no known route */
		kfree_skb(skb,FREE_READ);
		return 0;
	}
	i = r->ir_intrfc;
	(void)ipxitf_send(i, skb, (r->ir_routed) ? 
			r->ir_router_node : ipx->ipx_dest.node);
	return 0;
}

/*
 *	We use a normal struct rtentry for route handling
 */
 
static int ipxrtr_ioctl(unsigned int cmd, void *arg)
{
	int err;
	struct rtentry rt;	/* Use these to behave like 'other' stacks */
	struct sockaddr_ipx *sg,*st;

	err=verify_area(VERIFY_READ,arg,sizeof(rt));
	if(err)
		return err;
		
	memcpy_fromfs(&rt,arg,sizeof(rt));
	
	sg=(struct sockaddr_ipx *)&rt.rt_gateway;
	st=(struct sockaddr_ipx *)&rt.rt_dst;
	
	if(!(rt.rt_flags&RTF_GATEWAY))
		return -EINVAL;		/* Direct routes are fixed */
	if(sg->sipx_family!=AF_IPX)
		return -EINVAL;
	if(st->sipx_family!=AF_IPX)
		return -EINVAL;
		
	switch(cmd)
	{
		case SIOCDELRT:
			return ipxrtr_delete(st->sipx_network);
		case SIOCADDRT:
		{
			struct ipx_route_definition f;
			f.ipx_network=st->sipx_network;
			f.ipx_router_network=sg->sipx_network;
			memcpy(f.ipx_router_node, sg->sipx_node, IPX_NODE_LEN);
			return ipxrtr_create(&f);
		}
		default:
			return -EINVAL;
	}
}

static char *
ipx_frame_name(unsigned short frame)
{
	switch (ntohs(frame)) {
	case ETH_P_IPX: return "EtherII";
	case ETH_P_802_2: return "802.2";
	case ETH_P_SNAP: return "SNAP";
	case ETH_P_802_3: return "802.3";
	default: return "None";
	}
}

static char *
ipx_device_name(ipx_interface *intrfc)
{
	return (intrfc->if_internal ? "Internal" :
		(intrfc->if_dev ? intrfc->if_dev->name : "Unknown"));
}

/* Called from proc fs */
int 
ipx_get_interface_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_interface *i;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/* Theory.. Keep printing in the same place until we pass offset */
	
	len += sprintf (buffer,"%-11s%-15s%-9s%-11s%s\n", "Network", 
		"Node_Address", "Primary", "Device", "Frame_Type");
	for (i = ipx_interfaces; i != NULL; i = i->if_next) {
		len += sprintf(buffer+len, "%08lX   ", ntohl(i->if_netnum));
		len += sprintf (buffer+len,"%02X%02X%02X%02X%02X%02X   ", 
				i->if_node[0], i->if_node[1], i->if_node[2],
				i->if_node[3], i->if_node[4], i->if_node[5]);
		len += sprintf(buffer+len, "%-9s", (i == ipx_primary_net) ?
			"Yes" : "No");
		len += sprintf (buffer+len, "%-11s", ipx_device_name(i));
		len += sprintf (buffer+len, "%s\n", 
			ipx_frame_name(i->if_dlink_type));

		/* Are we still dumping unwanted data then discard the record */
		pos=begin+len;
		
		if(pos<offset) {
			len=0;			/* Keep dumping into the buffer start */
			begin=pos;
		}
		if(pos>offset+length)		/* We have dumped enough */
			break;
	}
	
	/* The data in question runs from begin to begin+len */
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

int 
ipx_get_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_socket *s;
	ipx_interface *i;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	/* Theory.. Keep printing in the same place until we pass offset */
	
	len += sprintf (buffer,"%-15s%-28s%-10s%-10s%-7s%s\n", "Local_Address", 
			"Remote_Address", "Tx_Queue", "Rx_Queue", 
			"State", "Uid");
	for (i = ipx_interfaces; i != NULL; i = i->if_next) {
		for (s = i->if_sklist; s != NULL; s = s->next) {
			len += sprintf (buffer+len,"%08lX:%04X  ", 
				htonl(i->if_netnum),
				htons(s->ipx_port));
			if (s->state!=TCP_ESTABLISHED) {
				len += sprintf(buffer+len, "%-28s", "Not_Connected");
			} else {
				len += sprintf (buffer+len,
					"%08lX:%02X%02X%02X%02X%02X%02X:%04X  ", 
					htonl(s->ipx_dest_addr.net),
					s->ipx_dest_addr.node[0], s->ipx_dest_addr.node[1], 
					s->ipx_dest_addr.node[2], s->ipx_dest_addr.node[3], 
					s->ipx_dest_addr.node[4], s->ipx_dest_addr.node[5],
					htons(s->ipx_dest_addr.sock));
			}
			len += sprintf (buffer+len,"%08lX  %08lX  ", 
				s->wmem_alloc, s->rmem_alloc);
			len += sprintf (buffer+len,"%02X     %03d\n", 
				s->state, SOCK_INODE(s->socket)->i_uid);
		
			/* Are we still dumping unwanted data then discard the record */
			pos=begin+len;
		
			if(pos<offset)
			{
				len=0;			/* Keep dumping into the buffer start */
				begin=pos;
			}
			if(pos>offset+length)		/* We have dumped enough */
				break;
		}
	}
	
	/* The data in question runs from begin to begin+len */
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Remove unwanted header data from length */
	if(len>length)
		len=length;		/* Remove unwanted tail data from length */
	
	return len;
}

int ipx_rt_get_info(char *buffer, char **start, off_t offset, int length)
{
	ipx_route *rt;
	int len=0;
	off_t pos=0;
	off_t begin=0;

	len += sprintf (buffer,"%-11s%-13s%s\n", 
			"Network", "Router_Net", "Router_Node");
	for (rt = ipx_routes; rt != NULL; rt = rt->ir_next)
	{
		len += sprintf (buffer+len,"%08lX   ", ntohl(rt->ir_net));
		if (rt->ir_routed) {
			len += sprintf (buffer+len,"%08lX     %02X%02X%02X%02X%02X%02X\n", 
				ntohl(rt->ir_intrfc->if_netnum), 
				rt->ir_router_node[0], rt->ir_router_node[1], 
				rt->ir_router_node[2], rt->ir_router_node[3], 
				rt->ir_router_node[4], rt->ir_router_node[5]);
		} else {
			len += sprintf (buffer+len, "%-13s%s\n",
					"Directly", "Connected");
		}
		pos=begin+len;
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

/*******************************************************************************************************************\
*													            *
*	      Handling for system calls applied via the various interfaces to an IPX socket object		    *
*														    *
\*******************************************************************************************************************/
 
static int ipx_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		default:
			return(-EINVAL);
	}
}

static int ipx_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	ipx_socket *sk;
	int err,opt;
	
	sk=(ipx_socket *)sock->data;
	
	if(optval==NULL)
		return(-EINVAL);

	err=verify_area(VERIFY_READ,optval,sizeof(int));
	if(err)
		return err;
	opt=get_fs_long((unsigned long *)optval);
	
	switch(level)
	{
		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					sk->ipx_type=opt;
					return 0;
				default:
					return -EOPNOTSUPP;
			}
			break;
			
		case SOL_SOCKET:		
			return sock_setsockopt(sk,level,optname,optval,optlen);

		default:
			return -EOPNOTSUPP;
	}
}

static int ipx_getsockopt(struct socket *sock, int level, int optname,
	char *optval, int *optlen)
{
	ipx_socket *sk;
	int val=0;
	int err;
	
	sk=(ipx_socket *)sock->data;

	switch(level)
	{

		case SOL_IPX:
			switch(optname)
			{
				case IPX_TYPE:
					val=sk->ipx_type;
					break;
				default:
					return -ENOPROTOOPT;
			}
			break;
			
		case SOL_SOCKET:
			return sock_getsockopt(sk,level,optname,optval,optlen);
			
		default:
			return -EOPNOTSUPP;
	}
	err=verify_area(VERIFY_WRITE,optlen,sizeof(int));
	if(err)
		return err;
	put_fs_long(sizeof(int),(unsigned long *)optlen);
	err=verify_area(VERIFY_WRITE,optval,sizeof(int));
	put_fs_long(val,(unsigned long *)optval);
	return(0);
}

static int ipx_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

static void def_callback1(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

static void def_callback2(struct sock *sk, int len)
{
	if(!sk->dead)
	{
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket, 1);
	}
}

static int 
ipx_create(struct socket *sock, int protocol)
{
	ipx_socket *sk;
	sk=(ipx_socket *)kmalloc(sizeof(*sk),GFP_KERNEL);
	if(sk==NULL)
		return(-ENOMEM);
	switch(sock->type)
	{
		case SOCK_DGRAM:
			break;
		default:
			kfree_s((void *)sk,sizeof(*sk));
			return(-ESOCKTNOSUPPORT);
	}
	sk->dead=0;
	sk->next=NULL;
	sk->broadcast=0;
	sk->rcvbuf=SK_RMEM_MAX;
	sk->sndbuf=SK_WMEM_MAX;
	sk->wmem_alloc=0;
	sk->rmem_alloc=0;
	sk->inuse=0;
	sk->shutdown=0;
	sk->prot=NULL;	/* So we use default free mechanisms */
	sk->broadcast=0;
	sk->err=0;
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	sk->send_head=NULL;
	skb_queue_head_init(&sk->back_log);
	sk->state=TCP_CLOSE;
	sk->socket=sock;
	sk->type=sock->type;
	sk->ipx_type=0;		/* General user level IPX */
	sk->debug=0;
	sk->ipx_intrfc = NULL;
	memset(&sk->ipx_dest_addr,'\0',sizeof(sk->ipx_dest_addr));
	sk->ipx_port = 0;
	sk->mtu=IPX_MTU;
	
	if(sock!=NULL)
	{
		sock->data=(void *)sk;
		sk->sleep=sock->wait;
	}
	
	sk->state_change=def_callback1;
	sk->data_ready=def_callback2;
	sk->write_space=def_callback1;
	sk->error_report=def_callback1;

	sk->zapped=1;
	return 0;
}

static int ipx_release(struct socket *sock, struct socket *peer)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	if(sk==NULL)
		return(0);
	if(!sk->dead)
		sk->state_change(sk);
	sk->dead=1;
	sock->data=NULL;
	ipx_destroy_socket(sk);
	return(0);
}

static int ipx_dup(struct socket *newsock,struct socket *oldsock)
{
	return(ipx_create(newsock,SOCK_DGRAM));
}

static unsigned short 
ipx_first_free_socketnum(ipx_interface *intrfc)
{
	unsigned short	socketNum = intrfc->if_sknum;

	if (socketNum < IPX_MIN_EPHEMERAL_SOCKET)
		socketNum = IPX_MIN_EPHEMERAL_SOCKET;

	while (ipxitf_find_socket(intrfc, ntohs(socketNum)) != NULL)
		if (socketNum > IPX_MAX_EPHEMERAL_SOCKET)
			socketNum = IPX_MIN_EPHEMERAL_SOCKET;
		else
			socketNum++;

	intrfc->if_sknum = socketNum;
	return	ntohs(socketNum);
}
	
static int ipx_bind(struct socket *sock, struct sockaddr *uaddr,int addr_len)
{
	ipx_socket *sk;
	ipx_interface *intrfc;
	struct sockaddr_ipx *addr=(struct sockaddr_ipx *)uaddr;
	
	sk=(ipx_socket *)sock->data;
	
	if(sk->zapped==0)
		return -EIO;
		
	if(addr_len!=sizeof(struct sockaddr_ipx))
		return -EINVAL;
	
	intrfc = ipxitf_find_using_net(addr->sipx_network);
	if (intrfc == NULL)
		return -EADDRNOTAVAIL;

	if (addr->sipx_port == 0) {
		addr->sipx_port = ipx_first_free_socketnum(intrfc);
		if (addr->sipx_port == 0)
			return -EINVAL;
	}

	if(ntohs(addr->sipx_port)<IPX_MIN_EPHEMERAL_SOCKET && !suser())
		return -EPERM;	/* protect IPX system stuff like routing/sap */
	
	/* Source addresses are easy. It must be our network:node pair for
	   an interface routed to IPX with the ipx routing ioctl() */

	if(ipxitf_find_socket(intrfc, addr->sipx_port)!=NULL) {
		if(sk->debug)
			printk("IPX: bind failed because port %X in use.\n",
				(int)addr->sipx_port);
		return -EADDRINUSE;	   
	}

	sk->ipx_port=addr->sipx_port;
	ipxitf_insert_socket(intrfc, sk);
	sk->zapped=0;
	if(sk->debug)
		printk("IPX: socket is bound.\n");
	return 0;
}

static int ipx_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *addr;
	
	sk->state = TCP_CLOSE;	
	sock->state = SS_UNCONNECTED;

	if(addr_len!=sizeof(*addr))
		return(-EINVAL);
	addr=(struct sockaddr_ipx *)uaddr;
	
	if(sk->ipx_port==0)
	/* put the autobinding in */
	{
		struct sockaddr_ipx uaddr;
		int ret;
	
		uaddr.sipx_port = 0;
		uaddr.sipx_network = 0L; 
		ret = ipx_bind (sock, (struct sockaddr *)&uaddr, sizeof(struct sockaddr_ipx));
		if (ret != 0) return (ret);
	}
	
	if(ipxrtr_lookup(addr->sipx_network)==NULL)
		return -ENETUNREACH;
	sk->ipx_dest_addr.net=addr->sipx_network;
	sk->ipx_dest_addr.sock=addr->sipx_port;
	memcpy(sk->ipx_dest_addr.node,addr->sipx_node,IPX_NODE_LEN);
	sk->ipx_type=addr->sipx_type;
	sock->state = SS_CONNECTED;
	sk->state=TCP_ESTABLISHED;
	return 0;
}

static int ipx_socketpair(struct socket *sock1, struct socket *sock2)
{
	return(-EOPNOTSUPP);
}

static int ipx_accept(struct socket *sock, struct socket *newsock, int flags)
{
	if(newsock->data)
		kfree_s(newsock->data,sizeof(ipx_socket));
	return -EOPNOTSUPP;
}

static int ipx_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	ipx_address *addr;
	struct sockaddr_ipx sipx;
	ipx_socket *sk;
	
	sk=(ipx_socket *)sock->data;
	
	*uaddr_len = sizeof(struct sockaddr_ipx);
		
	if(peer) {
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		addr=&sk->ipx_dest_addr;
		sipx.sipx_network = addr->net;
		memcpy(sipx.sipx_node,addr->node,IPX_NODE_LEN);
		sipx.sipx_port = addr->sock;
	} else {
		if (sk->ipx_intrfc != NULL) {
			sipx.sipx_network = sk->ipx_intrfc->if_netnum;
			memcpy(sipx.sipx_node, sk->ipx_intrfc->if_node,
				IPX_NODE_LEN);
		} else {
			sipx.sipx_network = 0L;
			memset(sipx.sipx_node, '\0', IPX_NODE_LEN);
		}
		sipx.sipx_port = sk->ipx_port;
	}
		
	sipx.sipx_family = AF_IPX;
	sipx.sipx_type = sk->ipx_type;
	memcpy(uaddr,&sipx,sizeof(sipx));
	return 0;
}

#if 0
/*
 * User to dump IPX packets (debugging)
 */
void dump_data(char *str,unsigned char *d) {
  static char h2c[] = "0123456789ABCDEF";
  int l,i;
  char *p, b[64];
  for (l=0;l<16;l++) {
    p = b;
    for (i=0; i < 8 ; i++) {
      *(p++) = h2c[d[i] & 0x0f];
      *(p++) = h2c[(d[i] >> 4) & 0x0f];
      *(p++) = ' ';
    }
    *(p++) = '-';
    *(p++) = ' ';
    for (i=0; i < 8 ; i++)  *(p++) = ' '<= d[i] && d[i]<'\177' ? d[i] : '.';
    *p = '\000';
    d += i;
    printk("%s-%04X: %s\n",str,l*8,b);
  }
}

void dump_addr(char *str,ipx_address *p) {
  printk("%s: %08X:%02X%02X%02X%02X%02X%02X:%04X\n",
   str,ntohl(p->net),p->node[0],p->node[1],p->node[2],
   p->node[3],p->node[4],p->node[5],ntohs(p->sock));
}

void dump_hdr(char *str,ipx_packet *p) {
  printk("%s: CHKSUM=%04X SIZE=%d (%04X) HOPS=%d (%02X) TYPE=%02X\n",
   str,p->ipx_checksum,ntohs(p->ipx_pktsize),ntohs(p->ipx_pktsize),
   p->ipx_tctrl,p->ipx_tctrl,p->ipx_type);
  dump_addr("  IPX-DST",&p->ipx_dest);
  dump_addr("  IPX-SRC",&p->ipx_source);
}

void dump_pkt(char *str,ipx_packet *p) {
  dump_hdr(str,p);
  dump_data(str,(unsigned char *)p);
}
#endif

int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	/* NULL here for pt means the packet was looped back */
	ipx_interface	*intrfc;
	ipx_packet *ipx;
	
	ipx=(ipx_packet *)skb->h.raw;
	
	if(ipx->ipx_checksum!=IPX_NO_CHECKSUM) {
		/* We don't do checksum options. We can't really. Novell don't seem to have documented them.
		   If you need them try the XNS checksum since IPX is basically XNS in disguise. It might be
		   the same... */
		kfree_skb(skb,FREE_READ);
		return 0;
	}
	
	/* Too small */
	if(htons(ipx->ipx_pktsize)<sizeof(ipx_packet)) {
		kfree_skb(skb,FREE_READ);
		return 0;
	}
	
	/* Determine what local ipx endpoint this is */
	intrfc = ipxitf_find_using_phys(dev, pt->type);
	if (intrfc == NULL) {
		if (ipxcfg_auto_create_interfaces) {
			intrfc = ipxitf_auto_create(dev, pt->type);
		}

		if (intrfc == NULL) {
			/* Not one of ours */
			kfree_skb(skb,FREE_READ);
			return 0;
		}
	}

	return ipxitf_rcv(intrfc, skb);
}

static int ipx_sendto(struct socket *sock, void *ubuf, int len, int noblock,
	unsigned flags, struct sockaddr *usip, int addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *usipx=(struct sockaddr_ipx *)usip;
	struct sockaddr_ipx local_sipx;
	int retval;

	if (sk->zapped) return -EIO; /* Socket not bound */
	if(flags) return -EINVAL;
		
	if(usipx) {
		if(sk->ipx_port == 0) {
			struct sockaddr_ipx uaddr;
			int ret;

			uaddr.sipx_port = 0;
			uaddr.sipx_network = 0L; 
			ret = ipx_bind (sock, (struct sockaddr *)&uaddr, sizeof(struct sockaddr_ipx));
			if (ret != 0) return ret;
		}

		if(addr_len <sizeof(*usipx))
			return -EINVAL;
		if(usipx->sipx_family != AF_IPX)
			return -EINVAL;
	} else {
		if(sk->state!=TCP_ESTABLISHED)
			return -ENOTCONN;
		usipx=&local_sipx;
		usipx->sipx_family=AF_IPX;
		usipx->sipx_type=sk->ipx_type;
		usipx->sipx_port=sk->ipx_dest_addr.sock;
		usipx->sipx_network=sk->ipx_dest_addr.net;
		memcpy(usipx->sipx_node,sk->ipx_dest_addr.node,IPX_NODE_LEN);
	}
	
	retval = ipxrtr_route_packet(sk, usipx, ubuf, len);
	if (retval < 0) return retval;

	return len;
}

static int ipx_send(struct socket *sock, void *ubuf, int size, int noblock, unsigned flags)
{
	return ipx_sendto(sock,ubuf,size,noblock,flags,NULL,0);
}

static int ipx_recvfrom(struct socket *sock, void *ubuf, int size, int noblock,
		   unsigned flags, struct sockaddr *sip, int *addr_len)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	struct sockaddr_ipx *sipx=(struct sockaddr_ipx *)sip;
	struct ipx_packet *ipx = NULL;
	int copied = 0;
	int truesize;
	struct sk_buff *skb;
	int er;
	
	if(sk->err)
	{
		er= -sk->err;
		sk->err=0;
		return er;
	}
	
	if (sk->zapped)
		return -EIO;

	if(addr_len)
		*addr_len=sizeof(*sipx);

	skb=skb_recv_datagram(sk,flags,noblock,&er);
	if(skb==NULL)
		return er;

	ipx = (ipx_packet *)(skb->h.raw);
	truesize=ntohs(ipx->ipx_pktsize) - sizeof(ipx_packet);
	copied = (truesize > size) ? size : truesize;
	skb_copy_datagram(skb,sizeof(struct ipx_packet),ubuf,copied);
	
	if(sipx)
	{
		sipx->sipx_family=AF_IPX;
		sipx->sipx_port=ipx->ipx_source.sock;
		memcpy(sipx->sipx_node,ipx->ipx_source.node,IPX_NODE_LEN);
		sipx->sipx_network=ipx->ipx_source.net;
		sipx->sipx_type = ipx->ipx_type;
	}
	skb_free_datagram(skb);
	return(truesize);
}		

static int ipx_write(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ipx_send(sock,ubuf,size,noblock,0);
}


static int ipx_recv(struct socket *sock, void *ubuf, int size , int noblock,
	unsigned flags)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	if(sk->zapped)
		return -ENOTCONN;
	return ipx_recvfrom(sock,ubuf,size,noblock,flags,NULL, NULL);
}

static int ipx_read(struct socket *sock, char *ubuf, int size, int noblock)
{
	return ipx_recv(sock,ubuf,size,noblock,0);
}


static int ipx_shutdown(struct socket *sk,int how)
{
	return -EOPNOTSUPP;
}

static int ipx_select(struct socket *sock , int sel_type, select_table *wait)
{
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	return datagram_select(sk,sel_type,wait);
}

static int ipx_ioctl(struct socket *sock,unsigned int cmd, unsigned long arg)
{
	int err;
	long amount=0;
	ipx_socket *sk=(ipx_socket *)sock->data;
	
	switch(cmd)
	{
		case TIOCOUTQ:
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			if(err)
				return err;
			amount=sk->sndbuf-sk->wmem_alloc;
			if(amount<0)
				amount=0;
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		case TIOCINQ:
		{
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if((skb=skb_peek(&sk->receive_queue))!=NULL)
				amount=skb->len;
			err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(unsigned long));
			put_fs_long(amount,(unsigned long *)arg);
			return 0;
		}
		case SIOCADDRT:
		case SIOCDELRT:
			if(!suser())
				return -EPERM;
			return(ipxrtr_ioctl(cmd,(void *)arg));
		case SIOCSIFADDR:
		case SIOCGIFADDR:
		case SIOCAIPXITFCRT:
		case SIOCAIPXPRISLT:
			if(!suser())
				return -EPERM;
			return(ipxitf_ioctl(cmd,(void *)arg));
		case SIOCIPXCFGDATA: 
		{
			err=verify_area(VERIFY_WRITE,(void *)arg,
				sizeof(ipx_config_data));
			if(err) return err;
			return(ipxcfg_get_config_data((void *)arg));
		}
		case SIOCGSTAMP:
			if (sk)
			{
				if(sk->stamp.tv_sec==0)
					return -ENOENT;
				err=verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval));
				if(err)
					return err;
					memcpy_tofs((void *)arg,&sk->stamp,sizeof(struct timeval));
				return 0;
			}
			return -EINVAL;
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
			return -EINVAL;
		default:
			return(dev_ioctl(cmd,(void *) arg));
	}
	/*NOTREACHED*/
	return(0);
}

static struct proto_ops ipx_proto_ops = {
	AF_IPX,
	
	ipx_create,
	ipx_dup,
	ipx_release,
	ipx_bind,
	ipx_connect,
	ipx_socketpair,
	ipx_accept,
	ipx_getname,
	ipx_read,
	ipx_write,
	ipx_select,
	ipx_ioctl,
	ipx_listen,
	ipx_send,
	ipx_recv,
	ipx_sendto,
	ipx_recvfrom,
	ipx_shutdown,
	ipx_setsockopt,
	ipx_getsockopt,
	ipx_fcntl,
};

/* Called by ddi.c on kernel start up */

static struct packet_type ipx_8023_packet_type = 

{
	0,	/* MUTTER ntohs(ETH_P_8023),*/
	NULL,		/* All devices */
	ipx_rcv,
	NULL,
	NULL,
};
 
static struct packet_type ipx_dix_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IPX),*/
	NULL,		/* All devices */
	ipx_rcv,
	NULL,
	NULL,
};
 
static struct notifier_block ipx_dev_notifier={
	ipxitf_device_event,
	NULL,
	0
};


extern struct datalink_proto	*make_EII_client(void);
extern struct datalink_proto	*make_8023_client(void);

void ipx_proto_init(struct net_proto *pro)
{
	unsigned char	val = 0xE0;
	unsigned char	snapval[5] =  { 0x0, 0x0, 0x0, 0x81, 0x37 };

	(void) sock_register(ipx_proto_ops.family, &ipx_proto_ops);

	pEII_datalink = make_EII_client();
	ipx_dix_packet_type.type=htons(ETH_P_IPX);
	dev_add_pack(&ipx_dix_packet_type);

	p8023_datalink = make_8023_client();
	ipx_8023_packet_type.type=htons(ETH_P_802_3);
	dev_add_pack(&ipx_8023_packet_type);
	
	if ((p8022_datalink = register_8022_client(val, ipx_rcv)) == NULL)
		printk("IPX: Unable to register with 802.2\n");

	if ((pSNAP_datalink = register_snap_client(snapval, ipx_rcv)) == NULL)
		printk("IPX: Unable to register with SNAP\n");
	
	register_netdevice_notifier(&ipx_dev_notifier);
		
	printk("Swansea University Computer Society IPX 0.29 BETA for NET3.019\n");
	printk("IPX Portions Copyright (c) 1995 Caldera, Inc.\n");
}
#endif
