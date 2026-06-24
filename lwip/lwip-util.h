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

#ifndef LWIP_UTIL_H
#define LWIP_UTIL_H

#define LOOP_DEV_NAME   "lo"

#include <errno.h>

#include <lwip/netif.h>

void init_ifs (void *arg);

void inquire_device (struct netif *netif, ip4_addr_t * addr,
		     ip4_addr_t * netmask, ip4_addr_t * peer,
		     ip4_addr_t * broadcast, ip4_addr_t * gateway,
		     ip6_addr_t * addr6, uint8_t * addr6_prefix_len);
error_t configure_device (struct netif *netif, ip4_addr_t addr,
			  ip4_addr_t netmask, ip4_addr_t peer,
			  ip4_addr_t broadcast, ip4_addr_t gateway,
			  ip6_addr_t * addr6);

#endif /* LWIP_UTIL_H */
