/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the ARCnet interface.
 *
 * Version:	$Id: if_arcnet.h,v 1.2 1997/09/05 08:57:54 mj Exp $
 *
 * Author:	David Woodhouse <dwmw2@cam.ac.uk>
 *		Avery Pennarun <apenwarr@bond.net>	
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
#ifndef _LINUX_IF_ARCNET_H
#define _LINUX_IF_ARCNET_H


/*
 *	These are the defined ARCnet Protocol ID's.
 */

	/* RFC1201 Protocol ID's */
#define ARC_P_IP	212		/* 0xD4 */
#define ARC_P_ARP	213		/* 0xD5 */
#define ARC_P_RARP	214		/* 0xD6 */
#define ARC_P_IPX	250		/* 0xFA */
#define ARC_P_NOVELL_EC	236		/* 0xEC */

	/* Old RFC1051 Protocol ID's */
#define ARC_P_IP_RFC1051 240		/* 0xF0 */
#define ARC_P_ARP_RFC1051 241		/* 0xF1 */

	/* MS LanMan/WfWg protocol */
#define ARC_P_ETHER	0xE8

	/* Unsupported/indirectly supported protocols */
#define ARC_P_DATAPOINT_BOOT	0	/* very old Datapoint equipment */
#define ARC_P_DATAPOINT_MOUNT	1
#define ARC_P_POWERLAN_BEACON	8	/* Probably ATA-Netbios related */
#define ARC_P_POWERLAN_BEACON2	243
#define ARC_P_LANSOFT	251		/* 0xFB - what is this? */
#define ARC_P_ATALK	0xDD


/*
 *	This is an ARCnet frame header.
 */

struct archdr                       /* was struct HardHeader */
{
	u_char	source,		/* source ARCnet - filled in automagically */
		destination,	/* destination ARCnet - 0 for broadcast    */
		offset1,	/* offset of ClientData (256-byte packets) */
		offset2;	/* offset of ClientData (512-byte packets) */

};

#endif	/* _LINUX_IF_ARCNET_H */
