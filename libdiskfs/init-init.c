/*
   Copyright (C) 1994 Free Software Foundation

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

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include <device/device.h>

mach_port_t diskfs_host_priv;
mach_port_t diskfs_master_device;
mach_port_t diskfs_default_pager;
mach_port_t diskfs_control_port;
struct mapped_time_value *diskfs_mtime;

spin_lock_t diskfs_node_refcnt_lock = SPIN_LOCK_INITIALIZER;

/* Call this after arguments have been parsed to initialize the
   library.  */ 
void 
diskfs_init_diskfs (void)
{
  mach_port_t host, dev;
  memory_object_t obj;
  device_t timedev;
  
  _libports_initialize ();	/* XXX */

  if (diskfs_host_priv == MACH_PORT_NULL
      || diskfs_master_device == MACH_PORT_NULL)
    {
      get_privileged_ports (&host, &dev);
      if (diskfs_host_priv == MACH_PORT_NULL)
	diskfs_host_priv = host;
      else
	mach_port_deallocate (mach_task_self (), host);
      if (diskfs_master_device == MACH_PORT_NULL)
	diskfs_master_device = dev;
      else
	mach_port_deallocate (mach_task_self (), dev);
    }
  
  assert (diskfs_host_priv != MACH_PORT_NULL); /* XXX */
  assert (diskfs_master_device != MACH_PORT_NULL); /* XXX */

  ports_wire_threads = diskfs_host_priv;

  diskfs_control_port =
    ((struct port_info *)ports_allocate_port(sizeof (struct port_info),
					     PT_CTL))->port;
  
  diskfs_default_pager = MACH_PORT_NULL;
  vm_set_default_memory_manager (diskfs_host_priv, &diskfs_default_pager);
  
  device_open (diskfs_master_device, 0, "time", &timedev);
  device_map (timedev, VM_PROT_READ, 0, sizeof (mapped_time_value_t), &obj, 0);
  vm_map (mach_task_self (), (vm_address_t *)diskfs_mtime,
	  sizeof (mapped_time_value_t), 0, 1, obj, 0, 0, VM_PROT_READ,
	  VM_PROT_READ, VM_INHERIT_NONE);
  mach_port_deallocate (mach_task_self (), timedev);
  mach_port_deallocate (mach_task_self (), obj);
}
