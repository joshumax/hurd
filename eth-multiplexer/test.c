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

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <error.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h>

int
main(int argc , char *argv[])
{
  mach_port_t device;
  mach_port_t master_device;
  error_t err;

  err = get_privileged_ports (0, &master_device);
  if (err)
    error (2, err, "cannot get device master port");

  err = device_open (master_device, D_READ | D_WRITE, "eth0", &device);
  if (err)
    error (1, err, "device_open");
  printf ("the device port is %d\n", device);

  err = device_open (master_device, D_READ | D_WRITE, "eth0", &device);
  if (err)
    error (1, err, "device_open");
  printf ("the device port is %d\n", device);

  return 0;
}
