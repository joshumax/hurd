#ifndef	_HACK_IN_H_
#define _HACK_IN_H_

#include <netinet/in.h>

/* IP_MTU_DISCOVER values */
#define IP_PMTUDISC_DONT		0	/* Never send DF frames */
#define IP_PMTUDISC_WANT		1	/* Use per route hints	*/
#define IP_PMTUDISC_DO			2	/* Always DF		*/

/* These need to appear somewhere around here */
#define IP_DEFAULT_MULTICAST_TTL        1
#define IP_DEFAULT_MULTICAST_LOOP       1

struct ip_mreqn
{
	struct in_addr	imr_multiaddr;		/* IP multicast address of group */
	struct in_addr	imr_address;		/* local IP address of interface */
	int		imr_ifindex;		/* Interface index */
};

struct in_pktinfo
{
	int		ipi_ifindex;
	struct in_addr	ipi_spec_dst;
	struct in_addr	ipi_addr;
};


/* <asm/byteorder.h> contains the htonl type stuff.. */
#include <asm/byteorder.h>

#ifdef __KERNEL__
/* Some random defines to make it easier in the kernel.. */
#define LOOPBACK(x)	(((x) & htonl(0xff000000)) == htonl(0x7f000000))
#define MULTICAST(x)	(((x) & htonl(0xf0000000)) == htonl(0xe0000000))
#define BADCLASS(x)	(((x) & htonl(0xf0000000)) == htonl(0xf0000000))
#define ZERONET(x)	(((x) & htonl(0xff000000)) == htonl(0x00000000))
#define LOCAL_MCAST(x)	(((x) & htonl(0xFFFFFF00)) == htonl(0xE0000000))

#endif


#endif
