/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		INET protocol dispatch tables.
 *
 * Version:	@(#)protocol.c	1.0.5	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	: Ahah! udp icmp errors don't work because
 *				  udp_err is never called!
 *		Alan Cox	: Added new fields for init and ready for
 *				  proper fragmentation (_NO_ 4K limits!)
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include "ip.h"
#include "protocol.h"
#include "tcp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "icmp.h"
#include "udp.h"
#include <linux/igmp.h>


static struct inet_protocol tcp_protocol = {
  tcp_rcv,		/* TCP handler		*/
  NULL,			/* No fragment handler (and won't be for a long time) */
  tcp_err,		/* TCP error control	*/
  NULL,			/* next			*/
  IPPROTO_TCP,		/* protocol ID		*/
  0,			/* copy			*/
  NULL,			/* data			*/
  "TCP"			/* name			*/
};


static struct inet_protocol udp_protocol = {
  udp_rcv,		/* UDP handler		*/
  NULL,			/* Will be UDP fraglist handler */
  udp_err,		/* UDP error control	*/
  &tcp_protocol,	/* next			*/
  IPPROTO_UDP,		/* protocol ID		*/
  0,			/* copy			*/
  NULL,			/* data			*/
  "UDP"			/* name			*/
};


static struct inet_protocol icmp_protocol = {
  icmp_rcv,		/* ICMP handler		*/
  NULL,			/* ICMP never fragments anyway */
  NULL,			/* ICMP error control	*/
  &udp_protocol,	/* next			*/
  IPPROTO_ICMP,		/* protocol ID		*/
  0,			/* copy			*/
  NULL,			/* data			*/
  "ICMP"		/* name			*/
};

#ifndef CONFIG_IP_MULTICAST
struct inet_protocol *inet_protocol_base = &icmp_protocol;
#else
static struct inet_protocol igmp_protocol = {
  igmp_rcv,		/* IGMP handler		*/
  NULL,			/* IGMP never fragments anyway */
  NULL,			/* IGMP error control	*/
  &icmp_protocol,	/* next			*/
  IPPROTO_IGMP,		/* protocol ID		*/
  0,			/* copy			*/
  NULL,			/* data			*/
  "IGMP"		/* name			*/
};

struct inet_protocol *inet_protocol_base = &igmp_protocol;
#endif

struct inet_protocol *inet_protos[MAX_INET_PROTOS] = {
  NULL
};


struct inet_protocol *
inet_get_protocol(unsigned char prot)
{
  unsigned char hash;
  struct inet_protocol *p;

  hash = prot & (MAX_INET_PROTOS - 1);
  for (p = inet_protos[hash] ; p != NULL; p=p->next) {
	if (p->protocol == prot) return((struct inet_protocol *) p);
  }
  return(NULL);
}


void
inet_add_protocol(struct inet_protocol *prot)
{
  unsigned char hash;
  struct inet_protocol *p2;

  hash = prot->protocol & (MAX_INET_PROTOS - 1);
  prot ->next = inet_protos[hash];
  inet_protos[hash] = prot;
  prot->copy = 0;

  /* Set the copy bit if we need to. */
  p2 = (struct inet_protocol *) prot->next;
  while(p2 != NULL) {
	if (p2->protocol == prot->protocol) {
		prot->copy = 1;
		break;
	}
	p2 = (struct inet_protocol *) prot->next;
  }
}


int
inet_del_protocol(struct inet_protocol *prot)
{
  struct inet_protocol *p;
  struct inet_protocol *lp = NULL;
  unsigned char hash;

  hash = prot->protocol & (MAX_INET_PROTOS - 1);
  if (prot == inet_protos[hash]) {
	inet_protos[hash] = (struct inet_protocol *) inet_protos[hash]->next;
	return(0);
  }

  p = (struct inet_protocol *) inet_protos[hash];
  while(p != NULL) {
	/*
	 * We have to worry if the protocol being deleted is
	 * the last one on the list, then we may need to reset
	 * someone's copied bit.
	 */
	if (p->next != NULL && p->next == prot) {
		/*
		 * if we are the last one with this protocol and
		 * there is a previous one, reset its copy bit.
		 */
	     if (p->copy == 0 && lp != NULL) lp->copy = 0;
	     p->next = prot->next;
	     return(0);
	}

	if (p->next != NULL && p->next->protocol == prot->protocol) {
		lp = p;
	}

	p = (struct inet_protocol *) p->next;
  }
  return(-1);
}
