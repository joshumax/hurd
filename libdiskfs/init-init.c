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
#include <hurd/fsys.h>

mach_port_t diskfs_host_priv;
mach_port_t diskfs_master_device;
mach_port_t diskfs_default_pager;
mach_port_t diskfs_auth_server_port;
volatile struct mapped_time_value *diskfs_mtime;

spin_lock_t diskfs_node_refcnt_lock = SPIN_LOCK_INITIALIZER;

spin_lock_t _diskfs_control_lock = SPIN_LOCK_INITIALIZER;
int _diskfs_ncontrol_ports = 0;

/* XXX */
mach_port_t _diskfs_dotdot_file = MACH_PORT_NULL;

/* Call this after arguments have been parsed to initialize the
   library.  */ 
mach_port_t
diskfs_init_diskfs (mach_port_t bootstrap)
{
  mach_port_t host, dev;
  memory_object_t obj;
  device_t timedev;
  mach_port_t realnode;
  error_t err;
  
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
  
  assert (diskfs_master_device != MACH_PORT_NULL); /* XXX */

  diskfs_default_pager = MACH_PORT_NULL;
  vm_set_default_memory_manager (diskfs_host_priv, &diskfs_default_pager);

  if (bootstrap != MACH_PORT_NULL)
    {
      _diskfs_ncontrol_ports++;
      err = fsys_startup (bootstrap, 
			  ports_get_right (ports_allocate_port 
					   (sizeof (struct port_info), PT_CTL),
			  MACH_MSG_TYPE_MAKE_SEND,
			  &realnode);
      if (err)
	realnode = MACH_PORT_NULL;
    }
  else
    realnode = MACH_PORT_NULL;
  
  device_open (diskfs_master_device, 0, "time", &timedev);
  device_map (timedev, VM_PROT_READ, 0, sizeof (mapped_time_value_t), &obj, 0);
  vm_map (mach_task_self (), (vm_address_t *)&diskfs_mtime,
	  sizeof (mapped_time_value_t), 0, 1, obj, 0, 0, VM_PROT_READ,
	  VM_PROT_READ, VM_INHERIT_NONE);
  mach_port_deallocate (mach_task_self (), timedev);
  mach_port_deallocate (mach_task_self (), obj);

/*  diskfs_auth_server_port = getauth (); */

  return realnode;
}

void
_diskfs_control_clean (void *arg)
{
  spin_lock (&_diskfs_control_lock);
  _diskfs_ncontrol_ports--;
  spin_unlock (&_diskfs_control_lock);
}
