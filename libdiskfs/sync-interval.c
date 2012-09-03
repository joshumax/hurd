/* Support for periodic syncing

   Copyright (C) 1995,96,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <pthread.h>
#include <unistd.h>

#include <hurd/fsys.h>

#include "priv.h"

/* A user-readable variable reflecting the last set sync interval.  */
int diskfs_sync_interval = 0;

/* The thread that's doing the syncing.  */
static pthread_t periodic_sync_thread;

/* This port represents the periodic sync service as if it were
   an RPC.  We can use ports_inhibit_port_rpcs on this port to guarantee
   that the periodic_sync_thread is quiescent.  */
static struct port_info *pi;

static void * periodic_sync (void *);

/* Establish a thread to sync the filesystem every INTERVAL seconds, or
   never, if INTERVAL is zero.  If an error occurs creating the thread, it is
   returned, otherwise 0.  Subsequent calls will create a new thread and
   (eventually) get rid of the old one; the old thread won't do any more
   syncs, regardless.  */
error_t
diskfs_set_sync_interval (int interval)
{
  error_t err = 0;

  if (! pi)
    {
      err = ports_create_port (diskfs_control_class, diskfs_port_bucket,
			       sizeof (struct port_info), &pi);
      if (err)
	return err;
    }

  err = ports_inhibit_port_rpcs (pi);
  if (err)
    return err;

  /* Here we just set the new thread; any existing thread will notice when it
     wakes up and go away silently.  */
  if (interval == 0)
    periodic_sync_thread = 0;
  else
    {
      err = pthread_create (&periodic_sync_thread, NULL, periodic_sync,
			    (void *)(intptr_t) interval);
      if (!err)
        pthread_detach (periodic_sync_thread);
      else
	{
	  errno = err;
	  perror ("pthread_create");
	}
    }

  if (!err)
    diskfs_sync_interval = interval;

  ports_resume_port_rpcs (pi);

  return err;
}

/* ---------------------------------------------------------------- */

/* Sync the filesystem (pointed to by the variable CONTROL_PORT above) every
   INTERVAL seconds, as long as it's in the thread pointed to by the global
   variable PERIODIC_SYNC_THREAD.   */
static void *
periodic_sync (void * arg)
{
  int interval = (int) arg;
  for (;;)
    {
      error_t err;
      struct rpc_info link;

      /* This acts as a lock against creation of a new sync thread
	 while we are in the process of syncing.  */
      err = ports_begin_rpc (pi, 0, &link);

      if (periodic_sync_thread != pthread_self ())
	{
	  /* We've been superseded as the sync thread.  Just die silently.  */
	  ports_end_rpc (pi, &link);
	  return NULL;
	}

      if (! err)
	{
	  if (! diskfs_readonly)
	    {
	      pthread_rwlock_rdlock (&diskfs_fsys_lock);
	      /* Only sync if we need to, to avoid clearing the clean flag
		 when it's just been set.  Any other thread doing a sync
		 will have held the lock while it did its work.  */
	      if (_diskfs_diskdirty)
		{
		  diskfs_sync_everything (0);
		  diskfs_set_hypermetadata (0, 0);
		}
	      pthread_rwlock_unlock (&diskfs_fsys_lock);
	    }
	  ports_end_rpc (pi, &link);
	}

      /* Wait until next time.  */
      sleep (interval);
    }

  return NULL;
}
