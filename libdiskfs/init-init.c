/*
   Copyright (C) 1994, 1995 Free Software Foundation

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
#include <stdio.h>

mach_port_t diskfs_default_pager;
mach_port_t diskfs_auth_server_port;
volatile struct mapped_time_value *diskfs_mtime;

spin_lock_t diskfs_node_refcnt_lock = SPIN_LOCK_INITIALIZER;

spin_lock_t _diskfs_control_lock = SPIN_LOCK_INITIALIZER;
int _diskfs_ncontrol_ports = 0;

struct port_class *diskfs_protid_class;
struct port_class *diskfs_control_class;
struct port_class *diskfs_initboot_class;
struct port_class *diskfs_execboot_class;

struct port_bucket *diskfs_port_bucket;

/* Call this after arguments have been parsed to initialize the
   library.  */ 
error_t
diskfs_init_diskfs (void)
{
  error_t err;
  device_t timedev;
  memory_object_t obj;
  mach_port_t host, dev_master;
  
  err = get_privileged_ports (&host, &dev_master);
  if (err)
    return err;

  diskfs_default_pager = MACH_PORT_NULL;
  vm_set_default_memory_manager (host, &diskfs_default_pager);

  device_open (dev_master, 0, "time", &timedev);
  device_map (timedev, VM_PROT_READ, 0, sizeof (mapped_time_value_t), &obj, 0);
  vm_map (mach_task_self (), (vm_address_t *)&diskfs_mtime,
	  sizeof (mapped_time_value_t), 0, 1, obj, 0, 0, VM_PROT_READ,
	  VM_PROT_READ, VM_INHERIT_NONE);
  mach_port_deallocate (mach_task_self (), timedev);
  mach_port_deallocate (mach_task_self (), obj);

  diskfs_auth_server_port = getauth ();

  diskfs_protid_class = ports_create_class (diskfs_protid_rele, 0);
  diskfs_control_class = ports_create_class (_diskfs_control_clean, 0);
  diskfs_initboot_class = ports_create_class (0, 0);
  diskfs_execboot_class = ports_create_class (0, 0);
  diskfs_port_bucket = ports_create_bucket ();

  mach_port_deallocate (mach_task_self (), host);
  mach_port_deallocate (mach_task_self (), dev_master);

  return 0;
}

void
_diskfs_control_clean (void *arg __attribute__ ((unused)))
{
  spin_lock (&_diskfs_control_lock);
  _diskfs_ncontrol_ports--;
  spin_unlock (&_diskfs_control_lock);
}
