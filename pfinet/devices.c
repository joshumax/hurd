/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include <linux/netdevice.h>
#include <device/device.h>
#include <hurd.h>

struct device *dev_base;
struct device loopback_dev;

device_t master_device;

void
init_devices (void)
{
  error_t err;

  err = get_privileged_ports (0, &master_device);
  if (err)
    {
      perror ("Cannot fetch master device port");
      exit (1);
    }
  
  dev_base = 0;
}

void
add_device (struct device *dev)
{
  dev->next = dev_base;
  dev_base = dev;
}


  
