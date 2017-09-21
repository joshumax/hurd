/*
   Copyright (C) 2008 Free Software Foundation, Inc.
   Written by Zheng Da.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <execinfo.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if_ether.h>
#include <netinet/ip.h>

#include <mach.h>

#ifdef DEBUG

#define debug(format, ...) do				\
{							\
  fprintf (stderr, "eth-multiplexer: %s: ", __func__);  \
  fprintf (stderr, format, ## __VA_ARGS__);		\
  fprintf (stderr, "\n");                               \
  fflush (stderr);					\
} while (0)

#else

#define debug(format, ...) do {} while (0)

#endif

#define print_backtrace() do				\
{							\
  size_t size;						\
  void *array[30];					\
  size = backtrace (array, sizeof (array));		\
  debug ("the depth of the stack: %d", size);		\
  backtrace_symbols_fd(array, size, fileno (stderr));	\
} while (0)

static inline void
print_pack (char *packet, int len)
{
#ifdef DEBUG
#define ETH_P_IP 0x0800
  struct ethhdr *ethh = (struct ethhdr *) packet;
  struct iphdr *iph = (struct iphdr *)(ethh + 1);
  char src_str[INET_ADDRSTRLEN];
  char dst_str[INET_ADDRSTRLEN];
  if (ntohs (ethh->h_proto) == ETH_P_IP
      && len >= sizeof (struct ethhdr) + sizeof (struct iphdr))
    {
      debug ("multiplexer: get a IP packet from %s to %s\n",
	     inet_ntop (AF_INET, &iph->saddr, src_str, INET_ADDRSTRLEN),
	     inet_ntop (AF_INET, &iph->daddr, dst_str, INET_ADDRSTRLEN));
    }
  else
    {
      debug ("multiplexer: get a non-IP packet\n");
    }
#endif
}

#endif
