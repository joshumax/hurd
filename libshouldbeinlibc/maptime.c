/* Support for mach's mapped time

   Copyright (C) 1996 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <mach.h>
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
  device_t device;  
  mach_port_t mobj;

  if (use_mach_dev)
    {
      mach_port_t device_master;

      err = get_privileged_ports (0, &device_master);
      if (! err)
	{
	  err = device_open (device_master, 0, dev_name ?: "time", &device);
	  mach_port_deallocate (mach_task_self (), device_master);
	}
    }
  else
    {
      mach_msg_type_number_t data_len = 100;
      mach_msg_type_number_t num_ints = 10, num_ports = 10, num_offsets = 10;
      int _ints[num_ints], *ints = _ints;
      mach_port_t _ports[num_ports], *ports = _ports;
      off_t _offsets[num_offsets], *offsets = _offsets;
      char _data[data_len], *data = _data;
      file_t node = file_name_lookup (dev_name ?: "/dev/time", 0, 0);

      if (node == MACH_PORT_NULL)
	return errno;

      err = file_get_storage_info (node, &ports, &num_ports, &ints, &num_ints,
				   &offsets, &num_offsets, &data, &data_len);

      if (! err)
	{
	  int i;

	  if (num_ints >= 6 && ints[0] == STORAGE_DEVICE)
	    /* This a device.  */
	    if (num_ports != 1)
	      err = EGRATUITOUS;
	    else if (! MACH_PORT_VALID (ports[0]))
	      err = EPERM;	/* Didn't pass back the device port.  XXX */
	    else
	      {
		device = ports[0];
		ports[0] = MACH_PORT_NULL; /* Don't deallocate here.  */
	      }
	  else
	    err = ENODEV;	/* Not admitting to being a device.  XXX */

	  /* Deallocate any ports we got back.  */
	  for (i = 0; i < num_ports; i++)
	    if (MACH_PORT_VALID (ports[i]))
	      mach_port_deallocate (mach_task_self (), ports[i]);

	  /* Deallocate any out of line vectors return by gsi.  */ 
#define DISCARD_MEM(v, vl, b)						    \
	  if (vl && v != b)						    \
	    vm_deallocate (mach_task_self (), (vm_address_t)v, vl * sizeof *v);
	  DISCARD_MEM (ints, num_ints, _ints);
	  DISCARD_MEM (offsets, num_offsets, _offsets);
	  DISCARD_MEM (ports, num_ports, _ports);
	  DISCARD_MEM (data, data_len, _data);
	}

      mach_port_deallocate (mach_task_self (), node);
    }

  if (err)
    return err;

  err = device_map (device, VM_PROT_READ, 0, sizeof *mtime, &mobj, 0);
  if (! err)
    {
      err =
	vm_map (mach_task_self (), (vm_address_t *)mtime, sizeof *mtime, 0, 1,
		mobj, 0, 0, VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
      mach_port_deallocate (mach_task_self (), mobj);
    }

  mach_port_deallocate (mach_task_self (), device);

  return err;
}
