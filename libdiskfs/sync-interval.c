/* Support for periodic syncing

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <errno.h>
#include <cthreads.h>
#include <unistd.h>

#include <hurd/fsys.h>

#include "priv.h"

/* The thread that's doing the syncing.  */
cthread_t periodic_sync_thread = 0;

/* A lock to lock before changing any of the above.  */
spin_lock_t periodic_sync_lock = SPIN_LOCK_INITIALIZER;

/* The filesystem control port to which we send our sync requests.  */
mach_port_t control_port;


static void periodic_sync ();

/* Establish a thread to sync the filesystem every INTERVAL seconds, or
   never, if INTERVAL is zero.  If an error occurs creating the thread, it is
   returned, otherwise 0.  Subsequent calls will create a new thread and
   (eventually) get rid of the old one; the old thread won't do any more
   syncs, regardless.  */
error_t
diskfs_set_sync_interval (int interval)
{
  error_t err = 0;

  spin_lock (&periodic_sync_lock);

  if (control_port == MACH_PORT_NULL)
    {
      control_port =
	ports_get_right (ports_allocate_port
			 (diskfs_port_bucket, sizeof (struct port_info), 
			  diskfs_control_class));
      err =
	mach_port_insert_right (mach_task_self (),
				control_port, control_port,
				MACH_MSG_TYPE_MAKE_SEND);
    }

  if (!err)
    /* Here we just set the new thread; any existing thread will notice when it
       wakes up and go away silently.  */
    if (interval == 0)
      periodic_sync_thread = 0;
    else
      {
	periodic_sync_thread =
	  cthread_fork ((cthread_fn_t)periodic_sync, (any_t)interval);
	if (periodic_sync_thread)
	  cthread_detach (periodic_sync_thread);
	else
	  err = ED;
      }

  spin_unlock (&periodic_sync_lock);

  return 0;
}

/* ---------------------------------------------------------------- */

/* Sync the filesystem (pointed to by the variable CONTROL_PORT above) every
   INTERVAL seconds, as long as it's in the thread pointed to by the global
   variable PERIODIC_SYNC_THREAD.   */
static void
periodic_sync (int interval)
{
  for (;;)
    {
      cthread_t thread;

      spin_lock (&periodic_sync_lock);
      thread = periodic_sync_thread;
      spin_unlock (&periodic_sync_lock);

      if (thread != cthread_self ())
	/* We've been superseded as the sync thread...  Just die silently.  */
	return;

      fsys_syncfs (control_port, 0, 0);

      /* Wait until next time.  */
      sleep (interval);
    }
}
