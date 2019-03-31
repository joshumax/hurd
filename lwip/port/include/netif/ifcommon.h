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

/* Common interface for all kinds of devices */

#ifndef LWIP_IFCOMMON_H
#define LWIP_IFCOMMON_H

#include <stdint.h>
#include <sys/types.h>
#include <device/device.h>
#include <errno.h>

#include <lwip/netif.h>

/*
 * Helper struct to hold private data used to operate your interface.
 */
struct ifcommon
{
  uint16_t type;
  device_t ether_port;
  struct port_info *readpt;
  mach_port_t readptname;
  char *devname;
  uint16_t flags;

  /* Callbacks */
    err_t (*init) (struct netif * netif);
    error_t (*terminate) (struct netif * netif);
    error_t (*open) (struct netif * netif);
    error_t (*close) (struct netif * netif);
    error_t (*update_mtu) (struct netif * netif, uint32_t mtu);
    error_t (*change_flags) (struct netif * netif, uint16_t flags);
};

err_t if_init (struct netif *netif);
error_t if_terminate (struct netif *netif);
error_t if_change_flags (struct netif *netif, uint16_t flags);

/* Get the state from a netif */
#define netif_get_state(netif)  ((struct ifcommon *)netif->state)

#endif /* LWIP_IFCOMMON_H */
