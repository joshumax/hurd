/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the Ethernet IEEE 802.3 interface.
 *
 * Version:	@(#)if_ether.h	1.0.1a	02/08/94
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_ETHER_H
#define _LINUX_IF_ETHER_H


/* IEEE 802.3 Ethernet magic constants.  The frame sizes omit the preamble
   and FCS/CRC (frame check sequence). */
#define ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define ETH_HLEN	14		/* Total octets in header.	 */
#define ETH_ZLEN	60		/* Min. octets in frame sans FCS */
#define ETH_DATA_LEN	1500		/* Max. octets in payload	 */
#define ETH_FRAME_LEN	1514		/* Max. octets in frame sans FCS */


/* These are the defined Ethernet Protocol ID's. */
#define ETH_P_LOOP	0x0060		/* Ethernet Loopback packet	*/
#define ETH_P_ECHO	0x0200		/* Ethernet Echo packet		*/
#define ETH_P_PUP	0x0400		/* Xerox PUP packet		*/
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/
#define ETH_P_ARP	0x0806		/* Address Resolution packet	*/
#define ETH_P_RARP      0x8035		/* Reverse Addr Res packet	*/
#define ETH_P_X25	0x0805		/* CCITT X.25			*/
#define ETH_P_ATALK	0x809B		/* Appletalk DDP		*/
#define ETH_P_IPX	0x8137		/* IPX over DIX			*/
#define ETH_P_802_3	0x0001		/* Dummy type for 802.3 frames  */
#define ETH_P_AX25	0x0002		/* Dummy protocol id for AX.25  */
#define ETH_P_ALL	0x0003		/* Every packet (be careful!!!) */
#define ETH_P_802_2	0x0004		/* 802.2 frames 		*/
#define ETH_P_SNAP	0x0005		/* Internal only		*/
/* This is an Ethernet frame header. */
struct ethhdr {
  unsigned char		h_dest[ETH_ALEN];	/* destination eth addr	*/
  unsigned char		h_source[ETH_ALEN];	/* source ether addr	*/
  unsigned short	h_proto;		/* packet type ID field	*/
};

/* Ethernet statistics collection data. */
struct enet_statistics{
  int	rx_packets;			/* total packets received	*/
  int	tx_packets;			/* total packets transmitted	*/
  int	rx_errors;			/* bad packets received		*/
  int	tx_errors;			/* packet transmit problems	*/
  int	rx_dropped;			/* no space in linux buffers	*/
  int	tx_dropped;			/* no space available in linux	*/
  int	multicast;			/* multicast packets received	*/
  int	collisions;

  /* detailed rx_errors: */
  int	rx_length_errors;
  int	rx_over_errors;			/* receiver ring buff overflow	*/
  int	rx_crc_errors;			/* recved pkt with crc error	*/
  int	rx_frame_errors;		/* recv'd frame alignment error */
  int	rx_fifo_errors;			/* recv'r fifo overrun		*/
  int	rx_missed_errors;		/* receiver missed packet	*/

  /* detailed tx_errors */
  int	tx_aborted_errors;
  int	tx_carrier_errors;
  int	tx_fifo_errors;
  int	tx_heartbeat_errors;
  int	tx_window_errors;
};

#endif	/* _LINUX_IF_ETHER_H */
