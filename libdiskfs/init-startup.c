/* diskfs_startup_diskfs -- advertise our fsys control port to our parent FS.
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

/* Written by Roland McGrath.  */

#include "priv.h"
#include <stdio.h>
#include <string.h>
#include <hurd/fsys.h>
#include <hurd/startup.h>

mach_port_t
diskfs_startup_diskfs (mach_port_t bootstrap, int flags)
{
  mach_port_t realnode;
  struct port_info *newpi;
  
  if (! diskfs_readonly)
    /* Change to writable mode.  */
    diskfs_readonly_changed (0);

  if (bootstrap != MACH_PORT_NULL)
    {
      errno = ports_create_port (diskfs_control_class, diskfs_port_bucket,
				 sizeof (struct port_info), &newpi);
      if (! errno)
	{
	  errno = fsys_startup (bootstrap, flags, ports_get_right (newpi),
				MACH_MSG_TYPE_MAKE_SEND, &realnode);
	  ports_port_deref (newpi);
	}
      if (errno)
	{
	  perror ("Translator startup failure: fsys_startup");
	  exit (1);
	}
      mach_port_deallocate (mach_task_self (), bootstrap);
      _diskfs_ncontrol_ports++;

      _diskfs_init_completed ();
    }
  else
    {
      realnode = MACH_PORT_NULL;

      /* We are the bootstrap filesystem; do special boot-time setup.  */
      diskfs_start_bootstrap ();
    }

  if (diskfs_default_sync_interval)
    /* Start 'em sync'n */
    diskfs_set_sync_interval (diskfs_default_sync_interval);

  return realnode;
}

#if 0
error_t
diskfs_S_startup_dosync (mach_port_t handle)
{
  struct port_info *pi 
    = ports_lookup_port (diskfs_port_bucket,
			 diskfs_shutdown_notification_class);
  if (!pi)
    return EOPNOTSUPP;
  
  /* First start a sync so that if something goes wrong
     we at least get this much done. */
  diskfs_sync_everything (0);
  diskfs_set_hypermetadata (0, 0);
  
  rwlock_writer_lock (&diskfs_fsys_lock);
  
  /* Permit all the current RPC's to finish, and then suspend new ones */
  err = ports_inhibit_class_rpcs (diskfs_protid_class);
  if (err)
    return err;
  
  diskfs_shutdown_pager ();
  diskfs_set_hypermetadata (1, 1);
  
  return 0;
}
#endif

/* This is called when we have an ordinary environment, complete
   with proc and auth ports. */
void 
_diskfs_init_completed ()
{
  startup_t init;
  process_t proc;
  error_t err;
  struct port_info *pi;
  mach_port_t notify;

  /* Contact the startup server and register our shutdown request. 
     If we get an error, print an informational message. */

  proc = getproc ();
  assert (proc);
  
  err = ports_create_port (diskfs_shutdown_notification_class,
			   diskfs_port_bucket, sizeof (struct port_info),
			   &pi);
  if (err)
    goto errout;

  err = proc_getmsgport (proc, 1, &init);
  mach_port_deallocate (mach_task_self (), proc);
  if (err)
    goto errout;
  
  notify = ports_get_right (pi);
  ports_port_deref (pi);
  err = mach_port_insert_right (mach_task_self (), notify, notify,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), init);
      goto errout;
    }
  
  err = startup_request_notification (init, notify /* , name */);
  if (err)
    goto errout;
  
  mach_port_deallocate (mach_task_self (), init);
  mach_port_deallocate (mach_task_self (), notify);
  return;

 errout:
  fprintf (stderr, "Cannot request shutdown notification: %s\n",
	   strerror (err));
}

  
