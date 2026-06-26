/*
   Copyright (C) 2000,02,17 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Operations offered by the stack */

#include <lwip_pfinet_S.h>

#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <lwip/netif.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <sys/mman.h>
#include <net/route.h>

#include <lwip-util.h>
#include <stdlib.h>
#include <netif/hurdethif.h>

/*
 * Get all the data requested by SIOCGIFCONF for a particular interface.
 *
 * When ifc->ifc_ifreq == NULL, this function is being called for getting
 * the needed buffer length and not the actual data.
 */
static void
dev_ifconf (struct ifconf *ifc)
{
  struct netif *netif;
  struct ifreq *ifr;
  struct sockaddr_in *saddr;
  int len;

  ifr = ifc->ifc_req;
  len = ifc->ifc_len;
  saddr = (struct sockaddr_in *) &ifr->ifr_addr;
  NETIF_FOREACH(netif)
    {
      if (ifc->ifc_req != 0)
	{
	  /* Get the data */
	  if (len < (int) sizeof (struct ifreq))
	    break;

	  memset (ifr, 0, sizeof (struct ifreq));

	  strncpy (ifr->ifr_name, netif_get_state (netif)->devname,
		   sizeof (ifr->ifr_name)-1);
	  saddr->sin_len = sizeof (struct sockaddr_in);
	  saddr->sin_family = AF_INET;
	  saddr->sin_addr.s_addr = netif_ip4_addr (netif)->addr;

	  len -= sizeof (struct ifreq);
	}
      /* Update the needed buffer length */
      ifr++;
    }

  ifc->ifc_len = (uintptr_t) ifr - (uintptr_t) ifc->ifc_req;
}

/* Return the list of devices in the format provided by SIOCGIFCONF
   in IFR, but don't return more then AMOUNT bytes. If AMOUNT is
   negative, there is no limit.  */
kern_return_t
lwip_S_pfinet_siocgifconf (io_t port,
			   vm_size_t amount,
			   char **ifr, mach_msg_type_number_t * len)
{
  struct ifconf ifc;

  if (amount == (vm_size_t) - 1)
    {
      /* Get the needed buffer length */
      ifc.ifc_buf = 0;
      ifc.ifc_len = 0;
      dev_ifconf (&ifc);
      amount = ifc.ifc_len;
    }
  else
    ifc.ifc_len = amount;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer */
      if (*len < amount)
	{
	  void *buf = mmap (0, amount, PROT_READ | PROT_WRITE,
			    MAP_ANON, 0, 0);
	  if (buf == MAP_FAILED)
	    return ENOMEM;

	  ifc.ifc_buf = buf;
	}
      else
	ifc.ifc_buf = *ifr;

      dev_ifconf (&ifc);
    }

  *len = ifc.ifc_len;
  *ifr = ifc.ifc_buf;

  return 0;
}

/* pfinet_getroutes must return up to 255 routes */
#define MAX_ROUTES	255

static void
add_route(ifrtreq_t *rtable, char *devname, uint32_t dest, uint32_t netmask, uint32_t gw) {
  strncpy (rtable->ifname, devname, IF_NAMESIZE - 1);
  rtable->ifname[IF_NAMESIZE-1] = 0;
  rtable->rt_dest = dest;
  rtable->rt_mask = netmask;
  rtable->rt_gateway = gw;
}

static uint32_t get_routes(ifrtreq_t *rtable) {
  ifrtreq_t *rtable_it;
  struct netif *netif;
  char *devname;
  uint32_t addr, netmask, gw, count;

  rtable_it = rtable;
  count = 0;

  /* Add the default route if any
   * e.g. `0.0.0.0/0 via 192.168.1.1` */
  if (netif_default != NULL) {
    inquire_device (netif_default, 0, 0, 0, 0, &gw, 0, 0);

    if (gw != INADDR_ANY && gw != INADDR_NONE) {
      devname = netif_get_state (netif_default)->devname;
      add_route(rtable_it++, devname, INADDR_ANY, INADDR_ANY, gw);
      count++;
    }
  }

  /* Add subnet routes
   * e.g. `192.168.1.0/24 dev eth0` */
  NETIF_FOREACH (netif)
  {
    inquire_device (netif, &addr, &netmask, 0, 0, &gw, 0, 0);

    if(addr != INADDR_ANY && addr != INADDR_NONE
      && netmask != INADDR_ANY && netmask != INADDR_NONE) {
      devname = netif_get_state (netif)->devname;
      add_route(rtable_it++, devname, addr & netmask, netmask, INADDR_ANY);
      count++;
      }

    if (count == MAX_ROUTES)
      break;
  }

  return count;
}

/*
 * Return the routing table as a series of ifrtreq_t structs
 * in routes, but don't return more than `requested_amount` number of them.
 * If `requested_amount` is -1, we get up to `MAX_ROUTES`.
 *
 * Here we must translate from lwip internal routing into standard
 * BSD routing, which is what the caller expects.
 *
 * We support only two route types:
 *  -  For all interfaces having a valid IP address and mask:
 *     - Return a subnet route
 *     - e.g. `192.168.1.0/24 dev eth0`
 *   - If the default interface has a gateway set
 *     - Return a default gateway
 *     - e.g. `0.0.0.0/0 via 192.168.1.1`
 */
kern_return_t
lwip_S_pfinet_getroutes (io_t port,
			 vm_size_t requested_amount,
			 data_t *routes,
			 mach_msg_type_number_t *len,
			 boolean_t *dealloc_data)
{
  ifrtreq_t *rtable;
  size_t buflen;
  uint32_t available_count, count_to_return;

  if (dealloc_data)
    *dealloc_data = FALSE;

  rtable = calloc (MAX_ROUTES, sizeof (ifrtreq_t));

  available_count = get_routes (rtable);

  if (requested_amount == (vm_size_t) - 1)
  {
    /* All available */
    count_to_return = available_count;
  }
  else
    /* Minimum of requested and available */
    count_to_return = requested_amount > available_count ? available_count : requested_amount;

  buflen = count_to_return * sizeof (ifrtreq_t);

  /* Possibly allocate a new buffer. */
  if (*len < buflen)
  {
    *routes = mmap (0, buflen,PROT_READ | PROT_WRITE,
        MAP_ANON, 0, 0);

    if (*routes == MAP_FAILED)
    {
      *len = 0;
      return ENOMEM;
    }

    if (dealloc_data)
      *dealloc_data = TRUE;
  }

  memcpy(*routes, rtable, buflen);
  *len = buflen;

  free (rtable);

  return 0;
}
