/* diskfs_startup_diskfs -- advertise our fsys control port to our parent FS.
   Copyright (C) 1994,95,96,98,99,2000,02 Free Software Foundation

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
#include <fcntl.h>
#include <error.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>
#include <hurd/startup.h>

#include "startup_S.h"

char *_diskfs_chroot_directory;

mach_port_t
diskfs_startup_diskfs (mach_port_t bootstrap, int flags)
{
  error_t err;
  mach_port_t realnode, right;
  struct port_info *newpi;

  if (_diskfs_chroot_directory != NULL)
    {
      /* The boot options requested we change to a subdirectory
	 and treat that as the root of the filesystem.  */
      struct node *np, *old;
      struct protid *rootpi;
      struct peropen *rootpo;

      /* Skip leading slashes.  */
      while (*_diskfs_chroot_directory == '/')
	++_diskfs_chroot_directory;

      pthread_mutex_lock (&diskfs_root_node->lock);

      /* Create a protid we can use in diskfs_lookup.  */
      err = diskfs_make_peropen (diskfs_root_node, O_READ|O_EXEC,
				 0, &rootpo);
      assert_perror_backtrace (err);
      err = diskfs_create_protid (rootpo, 0, &rootpi);
      assert_perror_backtrace (err);

      /* Look up the directory name.  */
      err = diskfs_lookup (diskfs_root_node, _diskfs_chroot_directory,
			   LOOKUP, &np, NULL, rootpi);
      pthread_mutex_unlock (&diskfs_root_node->lock);
      ports_port_deref (rootpi);

      if (err == EAGAIN)
	error (1, 0, "`--virtual-root=%s' specifies the real root directory",
	       _diskfs_chroot_directory);
      else if (err)
	error (1, err, "`%s' not found", _diskfs_chroot_directory);

      if (!S_ISDIR (np->dn_stat.st_mode))
	{
	  pthread_mutex_unlock (&np->lock);
	  error (1, ENOTDIR, "%s", _diskfs_chroot_directory);
	}

      /* Install this node as the new root, forgetting about the real root
	 node.  The last essential piece that makes the virtual root work
	 is in fsys-getroot.c, which sets the first peropen's shadow_root
	 if _diskfs_chroot_directory is non-null.  */
      old = diskfs_root_node;
      diskfs_root_node = np;
      pthread_mutex_unlock (&np->lock);
      diskfs_nput (old);
    }

  if (bootstrap != MACH_PORT_NULL)
    {
      err = ports_create_port (diskfs_control_class, diskfs_port_bucket,
			       sizeof (struct port_info), &newpi);
      if (! err)
	{
	  right = ports_get_send_right (newpi);
	  err = fsys_startup (bootstrap, flags, right,
			      MACH_MSG_TYPE_COPY_SEND, &realnode);
	  mach_port_deallocate (mach_task_self (), right);
	  ports_port_deref (newpi);
	}
      if (err)
        error (1, err, "Translator startup failure: fsys_startup");

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

error_t
diskfs_S_startup_dosync (mach_port_t handle)
{
  error_t err = 0;
  struct port_info *pi
    = ports_lookup_port (diskfs_port_bucket, handle,
			 diskfs_shutdown_notification_class);

  if (!pi)
    return EOPNOTSUPP;

  if (! diskfs_readonly)
    {
      /* First start a sync so that if something goes wrong
	 we at least get this much done. */
      diskfs_sync_everything (0);
      diskfs_set_hypermetadata (0, 0);

      pthread_rwlock_wrlock (&diskfs_fsys_lock);

      /* Permit all the current RPC's to finish, and then suspend new ones */
      err = ports_inhibit_class_rpcs (diskfs_protid_class);
      if (! err)
	{
	  diskfs_sync_everything (1);
	  diskfs_set_hypermetadata (1, 1);
	  _diskfs_diskdirty = 0;

	  /* XXX: if some application writes something after that, we will
	   * crash. That is still better than creating pending writes before
	   * poweroff, and thus fsck on next reboot.
	   */
	  diskfs_readonly = 1;
	  diskfs_readonly_changed (1);

	  ports_resume_class_rpcs (diskfs_protid_class);
	}

      pthread_rwlock_unlock (&diskfs_fsys_lock);
    }

  ports_port_deref (pi);

  return err;
}

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
  char *name;

  /* Contact the startup server and register our shutdown request.
     If we get an error, print an informational message. */

  proc = getproc ();
  assert_backtrace (proc);

  err = ports_create_port (diskfs_shutdown_notification_class,
			   diskfs_port_bucket, sizeof (struct port_info),
			   &pi);
  if (err)
    goto errout;

  /* Mark us as important.  */
  err = proc_mark_important (proc);
  mach_port_deallocate (mach_task_self (), proc);
  /* This might fail due to permissions or because the old proc server
     is still running, ignore any such errors.  */
  if (err && err != EPERM && err != EMIG_BAD_ID)
    goto errout;

  init = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (init == MACH_PORT_NULL)
    {
      err = errno;
      goto errout;
    }

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  asprintf (&name,
	    "%s %s", program_invocation_short_name, diskfs_disk_name ?: "-");
  err = startup_request_notification (init, notify,
				      MACH_MSG_TYPE_COPY_SEND, name);
  mach_port_deallocate (mach_task_self (), notify);
  free (name);
  if (err)
    goto errout;

  mach_port_deallocate (mach_task_self (), init);
  return;

 errout:
  error (0, err, "Warning: cannot request shutdown notification");
}
