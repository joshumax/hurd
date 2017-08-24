/*
   Copyright (C) 1993,94,95,96,97,98,2002 Free Software Foundation

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
#include "fsys_S.h"
#include <hurd/fshelp.h>
#include <hurd/fsys.h>
#include <fcntl.h>

/* Implement fsys_getroot as described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_getroot (struct diskfs_control *pt,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       mach_port_t dotdot,
		       uid_t *uids,
		       size_t nuids,
		       uid_t *gids,
		       size_t ngids,
		       int flags,
		       retry_type *retry,
		       char *retryname,
		       file_t *returned_port,
		       mach_msg_type_name_t *returned_port_poly)
{
  error_t err = 0;
  mode_t type;
  struct protid *newpi;
  struct peropen *newpo;
  struct iouser user;
  struct peropen peropen_context =
  {
    root_parent: dotdot,
    shadow_root_parent: MACH_PORT_NULL,
    shadow_root: _diskfs_chroot_directory ? diskfs_root_node : NULL, /* XXX */
    path: NULL,
  };

  if (!pt)
    return EOPNOTSUPP;

  flags &= O_HURD;

  user.uids = make_idvec ();
  user.gids = make_idvec ();
  idvec_set_ids (user.uids, uids, nuids);
  idvec_set_ids (user.gids, gids, ngids);
#define drop_idvec() idvec_free (user.gids); idvec_free (user.uids)

  pthread_rwlock_rdlock (&diskfs_fsys_lock);
  pthread_mutex_lock (&diskfs_root_node->lock);

  /* This code is similar (but not the same as) the code in
     dir-lookup.c that does the same thing.  Perhaps a way should
     be found to share the logic.  */

  type = diskfs_root_node->dn_stat.st_mode & S_IFMT;

  if (((diskfs_root_node->dn_stat.st_mode & S_IPTRANS)
       || fshelp_translated (&diskfs_root_node->transbox))
      && !(flags & O_NOTRANS))
    {
      struct fshelp_stat_cookie2 cookie = {
	.next = &peropen_context,
      };

      err = fshelp_fetch_root (&diskfs_root_node->transbox,
			       &cookie, dotdot, &user, flags,
			       _diskfs_translator_callback1,
			       _diskfs_translator_callback2,
			       retry, retryname, returned_port);
      if (err != ENOENT)
	{
	  pthread_mutex_unlock (&diskfs_root_node->lock);
	  pthread_rwlock_unlock (&diskfs_fsys_lock);
	  drop_idvec ();
	  if (!err)
	    *returned_port_poly = MACH_MSG_TYPE_MOVE_SEND;
	  return err;
	}

      /* ENOENT means the translator was removed in the interim. */
      err = 0;
    }

  if (type == S_IFLNK && !(flags & (O_NOLINK | O_NOTRANS)))
    {
      /* Handle symlink interpretation */
      char pathbuf[diskfs_root_node->dn_stat.st_size + 1];

      if (diskfs_read_symlink_hook)
	err = (*diskfs_read_symlink_hook) (diskfs_root_node, pathbuf);
      if (!diskfs_read_symlink_hook || err == EINVAL)
	{
	  size_t amt = 0;
	  err = diskfs_node_rdwr (diskfs_root_node, pathbuf, 0,
				  diskfs_root_node->dn_stat.st_size, 0,
				  0, &amt);
	  pathbuf[amt] = '\0';
	}

      pthread_mutex_unlock (&diskfs_root_node->lock);
      pthread_rwlock_unlock (&diskfs_fsys_lock);
      if (err)
	{
	  drop_idvec ();
	  return err;
	}

      if (pathbuf[0] == '/')
	{
	  *retry = FS_RETRY_MAGICAL;
	  *returned_port = MACH_PORT_NULL;
	  *returned_port_poly = MACH_MSG_TYPE_COPY_SEND;
	  strcpy (retryname, pathbuf);
	  mach_port_deallocate (mach_task_self (), dotdot);
	  drop_idvec ();
	  return 0;
	}
      else
	{
	  *retry = FS_RETRY_REAUTH;
	  *returned_port = dotdot;
	  *returned_port_poly = MACH_MSG_TYPE_MOVE_SEND;
	  strcpy (retryname, pathbuf);
	  drop_idvec ();
	  return 0;
	}
    }

  if ((type == S_IFSOCK || type == S_IFBLK
       || type == S_IFCHR || type == S_IFIFO)
      && (flags & (O_READ|O_WRITE|O_EXEC)))
    err = EOPNOTSUPP;

  if (!err && (flags & O_READ))
    err = fshelp_access (&diskfs_root_node->dn_stat, S_IREAD, &user);

  if (!err && (flags & O_EXEC))
    err = fshelp_access (&diskfs_root_node->dn_stat, S_IEXEC, &user);

  if (!err && (flags & (O_WRITE)))
    {
      if (type == S_IFDIR)
	err = EISDIR;
      else if (diskfs_check_readonly ())
	err = EROFS;
      else
	err = fshelp_access (&diskfs_root_node->dn_stat,
			       S_IWRITE, &user);
    }

  if (err)
    {
      pthread_mutex_unlock (&diskfs_root_node->lock);
      pthread_rwlock_unlock (&diskfs_fsys_lock);
      drop_idvec ();
      return err;
    }

  if ((flags & O_NOATIME)
      && (fshelp_isowner (&diskfs_root_node->dn_stat, &user)
	  == EPERM))
    flags &= ~O_NOATIME;

  flags &= ~OPENONLY_STATE_MODES;

  err = diskfs_make_peropen (diskfs_root_node, flags,
			       &peropen_context, &newpo);
  if (! err)
    {
      err = diskfs_create_protid (newpo, &user, &newpi);
      if (err)
	diskfs_release_peropen (newpo);
    }

  if (! err)
    {
      mach_port_deallocate (mach_task_self (), dotdot);
      *retry = FS_RETRY_NORMAL;
      *retryname = '\0';
      *returned_port = ports_get_right (newpi);
      *returned_port_poly = MACH_MSG_TYPE_MAKE_SEND;
      ports_port_deref (newpi);
    }

  pthread_mutex_unlock (&diskfs_root_node->lock);
  pthread_rwlock_unlock (&diskfs_fsys_lock);

  drop_idvec ();

  return err;
}
