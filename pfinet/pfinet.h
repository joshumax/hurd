/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include <device/device.h>
#include <hurd/ports.h>
#include <linux/netdevice.h>

extern device_t master_device;

void incoming_net_packet (void);

extern struct proto_ops *proto_ops;

struct mutex global_lock;

struct port_bucket *pfinet_bucket;

extern struct device ether_dev;

/* A port on SOCK.  Multiple sock_user's can point to the same socket. */
struct sock_user
{
  struct port_info pi;
  int isroot;
  struct socket *sock;		/* Linux socket structure, see linux/net.h */
};

/* Socket address ports. */
struct sock_addr
{
  struct port_info pi;
  size_t len;
  char address[0];
};

int ethernet_demuxer (mach_msg_header_t *, mach_msg_header_t *);
void setup_ethernet_device (void);
