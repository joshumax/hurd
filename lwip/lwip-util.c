/*
   Copyright (C) 2017 Free Software Foundation, Inc.
   Written by Joan Lledó.

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

/* Lwip management module */

#include <lwip-util.h>

#include <error.h>
#include <net/if_arp.h>

#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/tcpip.h>

#include <lwip-hurd.h>
#include <options.h>
#include <netif/hurdethif.h>
#include <netif/hurdtunif.h>
#include <netif/hurdloopif.h>

/*
 * Detect the proper module for the given device name
 * and returns its init callback
 */
static error_t
create_netif_state (char *name, struct ifcommon *ifc)
{
  char *base_name;

  memset (ifc, 0, sizeof (struct ifcommon));

  base_name = strrchr (name, '/');
  if (base_name)
    base_name++;
  else
    base_name = name;

  if (strncmp (base_name, "tun", 3) == 0)
    ifc->init = hurdtunif_device_init;
  else
    ifc->init = hurdethif_device_init;

  /* Freed in the module terminate callback */
  ifc->devname = strndup (name, strlen (name));

  return errno;
}

/* Some checks for IPv4 configurations */
static int
ipv4config_is_valid (uint32_t addr, uint32_t netmask,
		     uint32_t gateway, uint32_t broadcast)
{
  /* Check whether the user provided a valid netmask */
  if (netmask != INADDR_NONE && !ip4_addr_netmask_valid (netmask))
    {
      error (0, 0, "Error: Invalid network mask.\n");
      return 0;
    }

  /* The given gateway, if any, must be in the same network as the address */
  if (gateway != INADDR_NONE && (gateway & netmask) != (addr & netmask))
    {
      error (0, 0,
	     "Error: the gateway is not in the same network as the address.\n");
      return 0;
    }

  /*
   * LwIP doesn't allow setting the broadcast address.
   * We must ensure the given broadcast address is the default one for this
   * network.
   */
  if (broadcast != INADDR_NONE
      && netmask != INADDR_NONE && broadcast != (addr | ~netmask))
    {
      error (0, 0,
	     "Error: the broadcast address doesn't match the network mask.\n");
      return 0;
    }

  return 1;
}

/* Configure the loopback interface */
static void
init_loopback ()
{
  struct ifcommon ifc;

  memset (&ifc, 0, sizeof (struct ifcommon));
  ifc.init = hurdloopif_device_init;
  netif_list->state = &ifc;

  if_init (netif_list);
}

/* Remove the existing interfaces, but the loopback one */
void
remove_ifs ()
{
  struct netif *netif;

  netif = netif_list;
  while (netif != 0)
    {
      /* Skip the loopback interface */
      if (netif_get_state (netif)->type == ARPHRD_LOOPBACK)
	{
	  netif = netif->next;
	  continue;
	}
      if_terminate (netif);
      netif_remove (netif);
      free (netif);

      netif = netif_list;
    }

  return;
}

/* Initialize the interfaces given by the user through command line */
void
init_ifs (void *arg)
{
  struct parse_interface *in;
  struct parse_hook *ifs;
  struct netif *netif;
  struct ifcommon ifc;
  int8_t ipv6_addr_idx;
  ip6_addr_t *address6;
  int i;

  if (netif_list != 0)
    {
      if (netif_list->next == 0)
	init_loopback ();
      else
	remove_ifs ();
    }

  /*
   * Go through the list backwards. For LwIP
   * to create its list in the proper order.
   */
  ifs = (struct parse_hook *) arg;
  for (in = ifs->interfaces + ifs->num_interfaces - 1;
       in >= ifs->interfaces; in--)
    {
      /* The interface hasn't been completely configured */
      if (!in->dev_name[0])
	continue;

      if (!ipv4config_is_valid (in->address.addr, in->netmask.addr,
				in->gateway.addr, INADDR_NONE))
	continue;

      netif = calloc (1, sizeof (struct netif));

      create_netif_state (in->dev_name, &ifc);

      /*
       * Create a new interface and configre IPv4.
       *
       * Fifth parameter (ifc) is a hook.
       */
      if (!netif_add (netif, &in->address, &in->netmask, &in->gateway, &ifc,
			if_init, tcpip_input))
	{
	  /* The interface failed to init */
	  if (netif->state != &ifc)
	    /* It failed after setting the control block, must free it */
	    mem_free (netif->state);
	  free (netif);
	  continue;
	}

      /* Add IPv6 configuration */
      netif->ip6_autoconfig_enabled = 1;
      netif_create_ip6_linklocal_address (netif, 1);

      /* Add user given unicast addresses */
      for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
	{
	  address6 = (ip6_addr_t *) & in->addr6[i];

	  if (!ip6_addr_isany (address6) && !ip6_addr_ismulticast (address6))
	    {
	      netif_add_ip6_address (netif, address6, &ipv6_addr_idx);

	      if (ipv6_addr_idx >= 0)
		/* First use DAD to make sure nobody else has it */
		netif_ip6_addr_set_state (netif, ipv6_addr_idx,
					  IP6_ADDR_TENTATIVE);
	      else
		error (0, 0, "No free slot for IPv6 address: %s\n",
		       ip6addr_ntoa (address6));
	    }
	}

      /* Up the inerface */
      netif_set_up (netif);

      /* Set the first interface with valid gateway as default */
      if (in->gateway.addr != INADDR_NONE)
	{
	  netif_set_default (netif);
	}
    }

  /* Free the hook */
  free (ifs->interfaces);
  free (ifs);

  return;
}

/* Args for update_if() */
struct update_if_args
{
  struct netif *netif;
  uint32_t addr;
  uint32_t netmask;
  uint32_t peer;
  uint32_t broadcast;
  uint32_t gateway;
  uint32_t *addr6;
  uint8_t *addr6_prefix_len;
};

/*
 * Change the IP configuration of an interface
 */
static void
update_if (void *arg)
{
  int i;
  struct update_if_args *args = arg;

  netif_set_addr (args->netif, (ip4_addr_t *) & args->addr,
			   (ip4_addr_t *) & args->netmask,
			   (ip4_addr_t *) & args->gateway);

  if (args->addr6)
    for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
      {
	ip6_addr_t *laddr6 = ((ip6_addr_t *) args->addr6 + i);
	if (!ip6_addr_isany (laddr6))
	  {
	    netif_ip6_addr_set (args->netif, i, laddr6);

	    if (!ip6_addr_islinklocal (laddr6))
	      netif_ip6_addr_set_state (args->netif, i, IP6_ADDR_TENTATIVE);
	  }
      }

  if (args->addr6_prefix_len)
    for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
      *(args->addr6_prefix_len + i) = 64;

  free (args);

  return;
}

/* Get the IP configuration of an interface */
void
inquire_device (struct netif *netif, uint32_t * addr, uint32_t * netmask,
		uint32_t * peer, uint32_t * broadcast, uint32_t * gateway,
		uint32_t * addr6, uint8_t * addr6_prefix_len)
{
  int i;

  if (netif)
    {
      if (addr)
	*addr = netif_ip4_addr (netif)->addr;

      if (netmask)
	*netmask = netif_ip4_netmask (netif)->addr;

      if (peer)
	*peer = INADDR_NONE;

      if (broadcast)
	*broadcast =
	  netif_ip4_addr (netif)->addr | ~netif_ip4_netmask (netif)->addr;

      if (gateway)
	*gateway = netif_ip4_gw (netif)->addr;

      if (addr6)
	for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
	  {
	    *(addr6 + i * 4 + 0) = netif_ip6_addr (netif, i)->addr[0];
	    *(addr6 + i * 4 + 1) = netif_ip6_addr (netif, i)->addr[1];
	    *(addr6 + i * 4 + 2) = netif_ip6_addr (netif, i)->addr[2];
	    *(addr6 + i * 4 + 3) = netif_ip6_addr (netif, i)->addr[3];
	  }

      if (addr6_prefix_len)
	for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
	  *(addr6_prefix_len + i) = 64;
    }
}

/*
 * Check and change the IP configuration of an interface.
 * Called from ioctls.
 */
error_t
configure_device (struct netif *netif, uint32_t addr, uint32_t netmask,
		  uint32_t peer, uint32_t broadcast, uint32_t gateway,
		  uint32_t * addr6, uint8_t * addr6_prefix_len)
{
  error_t err = 0;

  if (netmask != INADDR_NONE)
    /*
     * If broadcasting is enabled and we have a netmask lesser than 31 bits
     * long, we need to update the broadcast address too.
     */
    if ((netif->flags & NETIF_FLAG_BROADCAST)
	&& ip4_addr_netmask_valid (netmask) && netmask <= 0xfffffffc)
      broadcast = (addr | ~netmask);

  if (!ipv4config_is_valid (addr, netmask, gateway, broadcast))
    err = EINVAL;
  else
    {
      /* Call update_if() inside the tcpip_thread */
      struct update_if_args *arg = calloc (1, sizeof (struct update_if_args));
      arg->netif = netif;
      arg->addr = addr;
      arg->netmask = netmask;
      arg->peer = peer;
      arg->broadcast = broadcast;
      arg->gateway = gateway;
      arg->addr6 = addr6;
      arg->addr6_prefix_len = addr6_prefix_len;
      err = err_to_errno(tcpip_callback (update_if, arg));
    }

  return err;
}
