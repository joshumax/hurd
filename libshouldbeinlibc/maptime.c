/* Support for mach's mapped time

   Copyright (C) 1996, 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <fcntl.h>
#include <hurd.h>
#include <device/device.h>

#include "maptime.h"

/* Return the mach mapped time page in MTIME.  If USE_MACH_DEV is false, then
   the hurd time device DEV_NAME, or "/dev/time" if DEV_NAME is 0, is
   used.  If USE_MACH_DEV is true, the mach device DEV_NAME, or "time" if
   DEV_NAME is 0, is used; this is a privileged operation.  The mapped time
   may be converted to a struct timeval at any time using maptime_read.  */
error_t
maptime_map (int use_mach_dev, char *dev_name,
	     volatile struct mapped_time_value **mtime)
{
  error_t err;
  mach_port_t memobj;

  if (use_mach_dev)
    {
      device_t device;  
      mach_port_t device_master;

      err = get_privileged_ports (0, &device_master);
      if (err)
	return err;

      err = device_open (device_master, 0, dev_name ?: "time", &device);
      mach_port_deallocate (mach_task_self (), device_master);
      if (err)
	return err;

      err = device_map (device, VM_PROT_READ, 0, sizeof *mtime, &memobj, 0);

      /* Deallocate the device port.  The mapping is independent of
	 this port.  */
      mach_port_deallocate (mach_task_self (), device);
    }
  else
    {
      mach_port_t wr_memobj;
      file_t node = file_name_lookup (dev_name ?: "/dev/time", O_RDONLY, 0);

      if (node == MACH_PORT_NULL)
	return errno;

      err = io_map (node, &memobj, &wr_memobj);
      if (!err && wr_memobj != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), wr_memobj);

      mach_port_deallocate (mach_task_self (), node);
    }

  if (! err)
    {
      *mtime = 0;
      err =
	vm_map (mach_task_self (), (vm_address_t *)mtime, sizeof *mtime, 0, 1,
		memobj, 0, 0, VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
      mach_port_deallocate (mach_task_self (), memobj);
    }

  return err;
}
