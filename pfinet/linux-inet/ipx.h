
/*
 *	The following information is in its entirety obtained from:
 *
 *	Novell 'IPX Router Specification' Version 1.10 
 *		Part No. 107-000029-001
 *
 *	Which is available from ftp.novell.com
 */

#ifndef _NET_INET_IPX_H_
#define _NET_INET_IPX_H_

#include <linux/skbuff.h>
#include "datalink.h"
#include <linux/ipx.h>

typedef struct
{
	unsigned long net;
	unsigned char  node[IPX_NODE_LEN]; 
	unsigned short sock;
} ipx_address;

#define ipx_broadcast_node	"\377\377\377\377\377\377"

typedef struct ipx_packet
{
	unsigned short	ipx_checksum;
#define IPX_NO_CHECKSUM	0xFFFF
	unsigned short  ipx_pktsize;
	unsigned char   ipx_tctrl;
	unsigned char   ipx_type;
#define IPX_TYPE_UNKNOWN	0x00
#define IPX_TYPE_RIP		0x01	/* may also be 0 */
#define IPX_TYPE_SAP		0x04	/* may also be 0 */
#define IPX_TYPE_SPX		0x05	/* Not yet implemented */
#define IPX_TYPE_NCP		0x11	/* $lots for docs on this (SPIT) */
#define IPX_TYPE_PPROP		0x14	/* complicated flood fill brdcast [Not supported] */
	ipx_address	ipx_dest __attribute__ ((packed));
	ipx_address	ipx_source __attribute__ ((packed));
} ipx_packet;


typedef struct sock ipx_socket;

#include "ipxcall.h"
extern int ipx_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt);
extern void ipxrtr_device_down(struct device *dev);

typedef struct ipx_interface {
	/* IPX address */
	unsigned long	if_netnum;
	unsigned char	if_node[IPX_NODE_LEN];

	/* physical device info */
	struct device	*if_dev;
	struct datalink_proto	*if_dlink;
	unsigned short	if_dlink_type;

	/* socket support */
	unsigned short	if_sknum;
	ipx_socket	*if_sklist;

	/* administrative overhead */
	int		if_ipx_offset;
	unsigned char	if_internal;
	unsigned char	if_primary;
	
	struct ipx_interface	*if_next;
}	ipx_interface;

typedef struct ipx_route {
	unsigned long ir_net;
	ipx_interface *ir_intrfc;
	unsigned char ir_routed;
	unsigned char ir_router_node[IPX_NODE_LEN];
	struct ipx_route *ir_next;
}	ipx_route;

#define IPX_MIN_EPHEMERAL_SOCKET	0x4000
#define IPX_MAX_EPHEMERAL_SOCKET	0x7fff

#endif
