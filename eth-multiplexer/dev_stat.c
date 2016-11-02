 /*
  * Mach Operating System
  * Copyright (c) 1993-1989 Carnegie Mellon University
  * All Rights Reserved.
  *
  * Permission to use, copy, modify and distribute this software and its
  * documentation is hereby granted, provided that both the copyright
  * notice and this permission notice appear in all copies of the
  * software, derivative works or modified versions, and any portions
  * thereof, and that both notices appear in supporting documentation.
  *
  * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
  * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
  * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
  *
  * Carnegie Mellon requests users of this software to return to
  *
  *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
  *  School of Computer Science
  *  Carnegie Mellon University
  *  Pittsburgh PA 15213-3890
  *
  * any improvements or extensions that they make and grant Carnegie Mellon
  * the rights to redistribute these changes.
  */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	3/98
 *
 *	Network IO.
 *
 *	Packet filter code taken from vaxif/enet.c written
 *		CMU and Stanford.
 */

/* the code copied from device/net_io.c in Mach */

#include <string.h>
#include <arpa/inet.h>

#include <mach.h>

#include "vdev.h"

io_return_t
dev_getstat(struct vether_device *ifp, dev_flavor_t flavor,
		dev_status_t status, natural_t *count)
{
	switch (flavor) {
		case NET_STATUS:
			{
				register struct net_status *ns = (struct net_status *)status;

				if (*count < NET_STATUS_COUNT)
					return (D_INVALID_OPERATION);

				ns->min_packet_size = ifp->if_header_size;
				ns->max_packet_size = ifp->if_header_size + ifp->if_mtu;
				ns->header_format = ifp->if_header_format;
				ns->header_size = ifp->if_header_size;
				ns->address_size = ifp->if_address_size;
				ns->flags = ifp->if_flags;
				ns->mapped_size = 0;

				*count = NET_STATUS_COUNT;
				break;
			}
		case NET_ADDRESS:
			{
				register int	addr_byte_count;
				register int	addr_int_count;
				register int	i;

				addr_byte_count = ifp->if_address_size;
				addr_int_count = (addr_byte_count + (sizeof(int)-1))
					/ sizeof(int);

				if (*count < addr_int_count) {
					return (D_INVALID_OPERATION);
				}

				memcpy(status, ifp->if_address, addr_byte_count);
				if (addr_byte_count < addr_int_count * sizeof(int))
					memset((char *)status + addr_byte_count, 0,
							(addr_int_count * sizeof(int)
							 - addr_byte_count));

				for (i = 0; i < addr_int_count; i++) {
					register int word;

					word = status[i];
					status[i] = htonl(word);
				}
				*count = addr_int_count;
				break;
			}
		default:
			return (D_INVALID_OPERATION);
	}
	return (D_SUCCESS);
}
