/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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
#include <hurd/fsys.h>

struct mutex diskfs_shutdown_lock = MUTEX_INITIALIZER;

/* Shutdown the filesystem; flags are as for fsys_goaway. */
error_t 
diskfs_shutdown (int flags)
{
  int nports = -1;
  int err;

  error_t
    helper (struct node *np)
      {
	error_t error;
	mach_port_t control;

	error = fshelp_fetch_control (&np->transbox, &control);
	mutex_unlock (&np->lock);
	if (!error && (control != MACH_PORT_NULL))
	  {
	    error = fsys_goaway (control, flags);
	    mach_port_deallocate (mach_task_self (), control);
	  }
	else
	  error = 0;
	mutex_lock (&np->lock);
	return error;
      }
  
  if ((flags & FSYS_GOAWAY_UNLINK)
       && S_ISDIR (diskfs_root_node->dn_stat.st_mode))
    return EBUSY;

  if (flags & FSYS_GOAWAY_RECURSE)
    {
      err = diskfs_node_iterate (helper);
      if (err)
	return err;
    }

  mutex_lock (&diskfs_shutdown_lock);
  
  /* Permit all the current RPC's to finish, and then
     suspend new ones.  */
  ports_inhibit_class_rpcs (diskfs_protid_class);

  /* Unfortunately, we can't inhibit control ports, because
     we are running inside a control port RPC.  What to do?
     ports_count_class will prevent new protid's from being created;
     that will happily block getroot and getfile.  diskfs_shutdown_lock
     will block simultaneous attempts at goaway and set_options.  Only 
     syncfs remains; perhaps a special flag could be used, or it could
     also hold diskfs_shutdown_lock (which should probably then be
     renamed...).  */

  /* First, see if there are outstanding user ports. */
  nports = ports_count_class (diskfs_protid_class);
  if (((flags & FSYS_GOAWAY_FORCE) == 0) 
      && (nports || diskfs_pager_users ()))
    {
      ports_enable_class (diskfs_protid_class);
      mutex_unlock (&diskfs_shutdown_lock);
      return EBUSY;
    }

  if ((flags & FSYS_GOAWAY_NOSYNC) == 0)
    {
      diskfs_shutdown_pager ();
      diskfs_set_hypermetadata (1, 1);
    }

  return 0;
}
