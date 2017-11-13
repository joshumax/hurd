/*
   Copyright (C) 1996, 1997, 2000, 2001, 2006, 2007, 2017
     Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

/* Fsysopts and command line option parsing */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <argp.h>

#include <lwip/ip.h>
#include <lwip/netif.h>

#define DEV_NAME_LEN    256

/* Used to describe a particular interface during argument parsing.  */
struct parse_interface
{
  /* The network interface in question.  */
  char dev_name[DEV_NAME_LEN];

  /* New values to apply to it. (IPv4) */
  ip4_addr_t address, netmask, peer, gateway;

  /* New IPv6 configuration to apply. */
  uint32_t addr6[LWIP_IPV6_NUM_ADDRESSES][4];
};

/* Used to hold data during argument parsing.  */
struct parse_hook
{
  /* A list of specified interfaces and their corresponding options.  */
  struct parse_interface *interfaces;
  size_t num_interfaces;

  /* Interface to which options apply.  If the device field isn't filled in
     then it should be by the next --interface option.  */
  struct parse_interface *curint;
};

/* Lwip translator options.  Used for both startup and runtime.  */
static const struct argp_option options[] = {
  {"interface", 'i', "DEVICE", 0, "Network interface to use", 1},
  {0, 0, 0, 0, "These apply to a given interface:", 2},
  {"address", 'a', "ADDRESS", OPTION_ARG_OPTIONAL, "Set the network address"},
  {"netmask", 'm', "MASK", OPTION_ARG_OPTIONAL, "Set the netmask"},
  {"gateway", 'g', "ADDRESS", OPTION_ARG_OPTIONAL, "Set the default gateway"},
  {"ipv4", '4', "NAME", 0, "Put active IPv4 translator on NAME"},
  {"ipv6", '6', "NAME", 0, "Put active IPv6 translator on NAME"},
  {"address6", 'A', "ADDR/LEN", OPTION_ARG_OPTIONAL,
   "Set the global IPv6 address"},
  {0}
};

static const char doc[] = "Interface-specific options before the first \
interface specification apply to the first following interface; otherwise \
they apply to the previously specified interface.";

#endif // OPTIONS_H
