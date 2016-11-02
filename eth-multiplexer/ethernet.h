/*
   Copyright (C) 2008
   Free Software Foundation, Inc.

   Written by Zheng Da.

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

#ifndef ETHERNET_H
#define ETHERNET_H

#include <netinet/in.h>
#include <stdlib.h>

extern mach_port_t ether_port;

int ethernet_open (char *dev_name, device_t master_device,
		   struct port_bucket *etherport_bucket,
		   struct port_class *etherreadclass);
int ethernet_close (char *dev_name);
int ethernet_demuxer (mach_msg_header_t *inp,
		      mach_msg_header_t *outp);

#endif

