#ifndef GLUE_LINUX_IN6_H
#define GLUE_LINUX_IN6_H 1

#include <netinet/in.h>

#if 0
struct ipv6_mreq {
	/* IPv6 multicast address of group */
	struct in6_addr ipv6mr_multiaddr;

	/* local IPv6 address of interface */
	int		ipv6mr_ifindex;
};
#endif

/* In Linux's struct ipv6_mreq the second member is called ipv6mr_ifindex,
 * however it's called ipv6mr_interface in ours.
 */
#define ipv6mr_ifindex ipv6mr_interface

struct in6_flowlabel_req
{
	struct in6_addr	flr_dst;
	__u32	flr_label;
	__u8	flr_action;
	__u8	flr_share;
	__u16	flr_flags;
	__u16 	flr_expires;
	__u16	flr_linger;
	__u32	__flr_pad;
	/* Options in format of IPV6_PKTOPTIONS */
};

#define IPV6_FL_A_GET	0
#define IPV6_FL_A_PUT	1
#define IPV6_FL_A_RENEW	2

#define IPV6_FL_F_CREATE	1
#define IPV6_FL_F_EXCL		2

#define IPV6_FL_S_NONE		0
#define IPV6_FL_S_EXCL		1
#define IPV6_FL_S_PROCESS	2
#define IPV6_FL_S_USER		3
#define IPV6_FL_S_ANY		255


/*
 *	Bitmask constant declarations to help applications select out the 
 *	flow label and priority fields.
 *
 *	Note that this are in host byte order while the flowinfo field of
 *	sockaddr_in6 is in network byte order.
 */

#define IPV6_FLOWINFO_FLOWLABEL		0x000fffff
#define IPV6_FLOWINFO_PRIORITY		0x0ff00000

/* These defintions are obsolete */
#define IPV6_PRIORITY_UNCHARACTERIZED	0x0000
#define IPV6_PRIORITY_FILLER		0x0100
#define IPV6_PRIORITY_UNATTENDED	0x0200
#define IPV6_PRIORITY_RESERVED1		0x0300
#define IPV6_PRIORITY_BULK		0x0400
#define IPV6_PRIORITY_RESERVED2		0x0500
#define IPV6_PRIORITY_INTERACTIVE	0x0600
#define IPV6_PRIORITY_CONTROL		0x0700
#define IPV6_PRIORITY_8			0x0800
#define IPV6_PRIORITY_9			0x0900
#define IPV6_PRIORITY_10		0x0a00
#define IPV6_PRIORITY_11		0x0b00
#define IPV6_PRIORITY_12		0x0c00
#define IPV6_PRIORITY_13		0x0d00
#define IPV6_PRIORITY_14		0x0e00
#define IPV6_PRIORITY_15		0x0f00

/*
 *	IPv6 TLV options.
 */
#define IPV6_TLV_PAD0		0
#define IPV6_TLV_PADN		1
#define IPV6_TLV_ROUTERALERT	20
#define IPV6_TLV_JUMBO		194

/*
 *	IPV6 socket options
 */

#define IPV6_ADDRFORM		1
#define IPV6_PKTINFO		2
#define IPV6_HOPOPTS		3
#define IPV6_DSTOPTS		4
#define IPV6_RTHDR		5
#define IPV6_PKTOPTIONS		6
#define IPV6_CHECKSUM		7
#define IPV6_HOPLIMIT		8
#define IPV6_NEXTHOP		9
#define IPV6_AUTHHDR		10
#define IPV6_FLOWINFO		11

/* IPV6_MTU_DISCOVER values */
#define IPV6_PMTUDISC_DONT              0
#define IPV6_PMTUDISC_WANT              1
#define IPV6_PMTUDISC_DO                2

/* Flowlabel */
#define IPV6_FLOWLABEL_MGR	32
#define IPV6_FLOWINFO_SEND	33

#endif /* not GLUE_LINUX_IN6_H */
