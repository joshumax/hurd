/* Parse run-time options

   Copyright (C) 1995, 1996, 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <errno.h>
#include <argz.h>
#include <hurd/fsys.h>
#include <string.h>

#include "netfs.h"
#include "fsys_S.h"

/* This code is originally from libdiskfs; things surrounded by `#if NOT_YET'
   are pending libnetfs being fleshed out some more.  */

/* Implement fsys_set_options as described in <hurd/fsys.defs>. */
error_t
netfs_S_fsys_set_options (struct netfs_control *pt,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_type,
			  char *data, mach_msg_type_number_t data_len,
			  int do_children)
{
  error_t err = 0;
  if (!pt)
    return EOPNOTSUPP;

  error_t
    helper (struct node *np)
      {
	error_t error;
	mach_port_t control;

	error = fshelp_fetch_control (&np->transbox, &control);
	pthread_mutex_unlock (&np->lock);
	if (!error && (control != MACH_PORT_NULL))
	  {
	    error = fsys_set_options (control, data, data_len, do_children);
	    mach_port_deallocate (mach_task_self (), control);
	  }
	else
	  error = 0;
	pthread_mutex_lock (&np->lock);

	if ((error == MIG_SERVER_DIED) || (error == MACH_SEND_INVALID_DEST))
	  error = 0;

	return error;
      }

#if NOT_YET
  if (do_children)
    {
      pthread_rwlock_wrlock (&netfs_fsys_lock);
      err = netfs_node_iterate (helper);
      pthread_rwlock_unlock (&netfs_fsys_lock);
    }
#endif

  if (!err)
    {
#if NOT_YET
      pthread_rwlock_wrlock (&netfs_fsys_lock);
#endif
      err = netfs_set_options (data, data_len);
#if NOT_YET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
    }

  return err;
}
