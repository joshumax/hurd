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
#include "ethernet.h"

io_return_t
dev_getstat(struct vether_device *ifp, dev_flavor_t flavor,
		dev_status_t status, natural_t *count)
{
	switch (flavor) {
		case NET_FLAGS:
			if (*count != 1)
				return D_INVALID_SIZE;
			status[0] = ifp->if_flags;
			break;
		case NET_STATUS:
			{
				struct net_status *ns = (struct net_status *)status;

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
				int	addr_byte_count;
				int	addr_int_count;
				int	i;

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
					int word;

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

static
int wants_all_multi_p (struct vether_device *v)
{
  return !! (v->if_flags & IFF_ALLMULTI);
}

io_return_t
vdev_setstat (struct vether_device *ifp, dev_flavor_t flavor,
	      dev_status_t status, size_t count)
{
  error_t err = 0;
  short flags;
  short delta;

  switch (flavor) {
  case NET_STATUS:
    {
      struct net_status *ns = (struct net_status *) status;

      if (count != NET_STATUS_COUNT)
	return D_INVALID_SIZE;

      /* We allow only the flags to change.  */
      if (ns->min_packet_size != ifp->if_header_size
          || ns->max_packet_size != ifp->if_header_size + ifp->if_mtu
          || ns->header_format != ifp->if_header_format
          || ns->header_size != ifp->if_header_size
          || ns->address_size != ifp->if_address_size
          || ns->mapped_size != 0)
	return D_INVALID_OPERATION;

      flags = ns->flags;
      goto change_flags;
    }

  case NET_FLAGS:
    if (count != 1)
      return D_INVALID_SIZE;
    flags = status[0];

  change_flags:
    /* What needs to change?  */
    delta = flags ^ ifp->if_flags;

    /* Only allow specific flag changes.  */
    if ((delta
	 /* AIUI IFF_RUNNING shouldn't be toggle-able, but we let this slip.  */
	 & ~(IFF_UP | IFF_RUNNING | IFF_DEBUG | IFF_PROMISC | IFF_ALLMULTI))
	!= 0)
      return D_INVALID_OPERATION;


    if (! err && (delta & IFF_PROMISC))
      {
	/* The ethernet device is always in promiscuous mode, and we
	   forward all packets.  If this flag is cleared for a virtual
	   device, we should filter traffic based on observed MAC
	   addresses from this interface.  */
	/* XXX: Implement this.  */
      }

    if (! err && (delta & IFF_ALLMULTI))
      {
	/* We activate IFF_ALLMULTI if at least one virtual device
	   wants it, and deactivate it otherwise.  */
	if ((flags & IFF_ALLMULTI)
	    || foreach_dev_do (wants_all_multi_p))
	  err = eth_set_clear_flags (IFF_ALLMULTI, 0);
	else
	  err = eth_set_clear_flags (0, IFF_ALLMULTI);
      }

    if (! err)
      ifp->if_flags = flags;
    break;

  case NET_ADDRESS:
    {
      int addr_byte_count;
      int addr_int_count;
      int i;

      addr_byte_count = ifp->if_address_size;
      addr_int_count = (addr_byte_count + (sizeof(int)-1)) / sizeof(int);

      if (count != addr_int_count)
	return D_INVALID_SIZE;

      memcpy(ifp->if_address, status, addr_byte_count);
      for (i = 0; i < addr_int_count; i++) {
	int word;

	word = status[i];
	status[i] = htonl(word);
      }
      break;
    }

  default:
    return D_INVALID_OPERATION;
  }

  return err;
}
