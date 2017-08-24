/*
   Copyright (C) 1994, 95, 96, 97, 98, 99, 2001 Free Software Foundation, Inc.

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
#include <maptime.h>

/* For safe inlining of diskfs_node_disknode and
   diskfs_disknode_node.  */
size_t const _diskfs_sizeof_struct_node = sizeof (struct node);

mach_port_t diskfs_default_pager;
mach_port_t diskfs_auth_server_port;
volatile struct mapped_time_value *diskfs_mtime;

pthread_rwlock_t diskfs_fsys_lock = PTHREAD_RWLOCK_INITIALIZER;
mach_port_t diskfs_fsys_identity;

int _diskfs_nosuid, _diskfs_noexec;
int _diskfs_noatime;

struct hurd_port _diskfs_exec_portcell;

pthread_spinlock_t _diskfs_control_lock = PTHREAD_SPINLOCK_INITIALIZER;
int _diskfs_ncontrol_ports;

struct port_class *diskfs_protid_class;
struct port_class *diskfs_control_class;
struct port_class *diskfs_execboot_class;
struct port_class *diskfs_shutdown_notification_class;

struct port_bucket *diskfs_port_bucket;

/* Call this after arguments have been parsed to initialize the
   library.  */
error_t
diskfs_init_diskfs (void)
{
  error_t err;

  if (diskfs_boot_filesystem ())
    /* This is a boot filesystem, we have to do some things specially.  */
    {
      mach_port_t host;
      err = get_privileged_ports (&host, 0);
      if (! err)
	{
	  diskfs_default_pager = MACH_PORT_NULL;
	  err = vm_set_default_memory_manager (host, &diskfs_default_pager);
	  mach_port_deallocate (mach_task_self (), host);
	}
      if (err)
	return err;
    }

  /* First try to use /dev/time...  */
  err = maptime_map (0, NULL, &diskfs_mtime);
  if (err)
    /* ... and fall back to the Mach time device.  */
    err = maptime_map (1, NULL, &diskfs_mtime);
  if (err)
    return err;

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &diskfs_fsys_identity);
  if (err)
    return err;

  diskfs_auth_server_port = getauth ();

  diskfs_protid_class = ports_create_class (diskfs_protid_rele, 0);
  diskfs_control_class = ports_create_class (_diskfs_control_clean, 0);
  diskfs_execboot_class = ports_create_class (0, 0);
  diskfs_shutdown_notification_class = ports_create_class (0, 0);

  diskfs_port_bucket = ports_create_bucket ();

  _hurd_port_init (&_diskfs_exec_portcell, MACH_PORT_NULL);

  return 0;
}

void
_diskfs_control_clean (void *arg __attribute__ ((unused)))
{
  pthread_spin_lock (&_diskfs_control_lock);
  _diskfs_ncontrol_ports--;
  pthread_spin_unlock (&_diskfs_control_lock);
}
