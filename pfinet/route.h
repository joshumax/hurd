/*
   Copyright (C) 2022  Free Software Foundation, Inc.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#ifndef ROUTE_H_
#define ROUTE_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

typedef struct ifrtreq {
  char ifname[IFNAMSIZ];
  in_addr_t rt_dest;
  in_addr_t rt_mask;
  in_addr_t rt_gateway;
  int rt_flags;
  int rt_metric;
  int rt_mtu;
  int rt_window;
  int rt_irtt;
  int rt_tos;
  int rt_class;
} ifrtreq_t;

#endif
