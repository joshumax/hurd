/* Parse run-time options

   Copyright (C) 1995, 1996, 1998 Free Software Foundation, Inc.

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

#include "priv.h"
#include "fsys_S.h"

/* Implement fsys_set_options as described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_set_options (struct diskfs_control *pt,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   char *data, mach_msg_type_number_t len,
			   int do_children)
{
  error_t err = 0;

  error_t
    helper (struct node *np)
      {
	error_t error;
	mach_port_t control;

	error = fshelp_fetch_control (&np->transbox, &control);
	pthread_mutex_unlock (&np->lock);
	if (!error && (control != MACH_PORT_NULL))
	  {
	    error = fsys_set_options (control, data, len, do_children);
	    mach_port_deallocate (mach_task_self (), control);
	  }
	else
	  error = 0;
	pthread_mutex_lock (&np->lock);

	if ((error == MIG_SERVER_DIED) || (error == MACH_SEND_INVALID_DEST))
	  error = 0;
	return error;
      }

  if (!pt
      || pt->pi.class != diskfs_control_class)
    return EOPNOTSUPP;

  if (do_children)
    {
      pthread_rwlock_wrlock (&diskfs_fsys_lock);
      err = diskfs_node_iterate (helper);
      pthread_rwlock_unlock (&diskfs_fsys_lock);
    }

  if (!err)
    {
      pthread_rwlock_wrlock (&diskfs_fsys_lock);
      err = diskfs_set_options (data, len);
      pthread_rwlock_unlock (&diskfs_fsys_lock);
    }

  return err;
}

/* Implement fsys_get_options as described in <hurd/fsys.defs>. */
error_t
diskfs_S_fsys_get_options (struct diskfs_control *port,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   char **data, mach_msg_type_number_t *data_len)
{
  char *argz = 0;
  size_t argz_len = 0;
  error_t err;

  if (!port
      || port->pi.class != diskfs_control_class)
    return EOPNOTSUPP;

  err = argz_add (&argz, &argz_len, program_invocation_name);
  if (err)
    return err;

  pthread_rwlock_rdlock (&diskfs_fsys_lock);
  err = diskfs_append_args (&argz, &argz_len);
  pthread_rwlock_unlock (&diskfs_fsys_lock);

  if (! err)
    /* Move ARGZ from a malloced buffer into a vm_alloced one.  */
    err = iohelp_return_malloced_buffer (argz, argz_len, data, data_len);
  else
    free (argz);

  return err;
}
