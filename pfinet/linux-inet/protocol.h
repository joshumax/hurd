/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the protocol dispatcher.
 *
 * Version:	@(#)protocol.h	1.0.2	05/07/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Changes:
 *		Alan Cox	:	Added a name field and a frag handler
 *					field for later.
 */
 
#ifndef _PROTOCOL_H
#define _PROTOCOL_H


#define MAX_INET_PROTOS	32		/* Must be a power of 2		*/


/* This is used to register protocols. */
struct inet_protocol {
  int			(*handler)(struct sk_buff *skb, struct device *dev,
				   struct options *opt, unsigned long daddr,
				   unsigned short len, unsigned long saddr,
				   int redo, struct inet_protocol *protocol);
  int			(*frag_handler)(struct sk_buff *skb, struct device *dev,
				   struct options *opt, unsigned long daddr,
				   unsigned short len, unsigned long saddr,
				   int redo, struct inet_protocol *protocol);
  void			(*err_handler)(int err, unsigned char *buff,
				       unsigned long daddr,
				       unsigned long saddr,
				       struct inet_protocol *protocol);
  struct inet_protocol *next;
  unsigned char		protocol;
  unsigned char		copy:1;
  void			*data;
  char 			*name;
};


extern struct inet_protocol *inet_protocol_base;
extern struct inet_protocol *inet_protos[MAX_INET_PROTOS];


extern void		inet_add_protocol(struct inet_protocol *prot);
extern int		inet_del_protocol(struct inet_protocol *prot);


#endif	/* _PROTOCOL_H */
