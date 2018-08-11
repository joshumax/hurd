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

#include <lwip-util.h>
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
  for (netif = netif_list; netif != 0; netif = netif->next)
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
error_t
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
	ifc.ifc_buf = (char *) mmap (0, amount, PROT_READ | PROT_WRITE,
				     MAP_ANON, 0, 0);
      else
	ifc.ifc_buf = *ifr;

      dev_ifconf (&ifc);
    }

  *len = ifc.ifc_len;
  *ifr = ifc.ifc_buf;

  return 0;
}
