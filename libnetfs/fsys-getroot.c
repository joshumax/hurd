/*
   Copyright (C) 1996,97,2001,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "netfs.h"
#include "fsys_S.h"
#include "misc.h"
#include "callbacks.h"
#include <fcntl.h>
#include <hurd/fshelp.h>

error_t
netfs_S_fsys_getroot (struct netfs_control *pt,
		      mach_port_t reply,
		      mach_msg_type_name_t reply_type,
		      mach_port_t dotdot,
		      uid_t *uids, mach_msg_type_number_t nuids,
		      uid_t *gids, mach_msg_type_number_t ngids,
		      int flags,
		      retry_type *do_retry,
		      char *retry_name,
		      mach_port_t *retry_port,
		      mach_msg_type_name_t *retry_port_type)
{
  struct iouser *cred;
  error_t err;
  struct protid *newpi;
  mode_t type;
  struct peropen peropen_context =
    {
      root_parent: dotdot,
      path: NULL,
    };

  if (!pt)
    return EOPNOTSUPP;

  err = iohelp_create_complex_iouser (&cred, uids, nuids, gids, ngids);
  if (err)
    return err;

  flags &= O_HURD;

  pthread_mutex_lock (&netfs_root_node->lock);
  err = netfs_validate_stat (netfs_root_node, cred);
  if (err)
    goto out;

  type = netfs_root_node->nn_stat.st_mode & S_IFMT;

  if (((netfs_root_node->nn_stat.st_mode & S_IPTRANS)
       || fshelp_translated (&netfs_root_node->transbox))
      && !(flags & O_NOTRANS))
    {
      struct fshelp_stat_cookie2 cookie = {
	.next = &peropen_context,
      };

      err = fshelp_fetch_root (&netfs_root_node->transbox,
			       &cookie, dotdot, cred, flags,
			       _netfs_translator_callback1,
			       _netfs_translator_callback2,
			       do_retry, retry_name, retry_port);
      if (err != ENOENT)
	{
	  pthread_mutex_unlock (&netfs_root_node->lock);
	  iohelp_free_iouser (cred);
	  if (!err)
	    *retry_port_type = MACH_MSG_TYPE_MOVE_SEND;
	  return err;
	}
      /* ENOENT means translator has vanished inside fshelp_fetch_root. */
      err = 0;
    }

  if (type == S_IFLNK && !(flags & (O_NOLINK | O_NOTRANS)))
    {
      char pathbuf[netfs_root_node->nn_stat.st_size + 1];

      err = netfs_attempt_readlink (cred, netfs_root_node, pathbuf);

      if (err)
	goto out;

      pthread_mutex_unlock (&netfs_root_node->lock);
      iohelp_free_iouser (cred);

      if (pathbuf[0] == '/')
	{
	  *do_retry = FS_RETRY_MAGICAL;
	  *retry_port = MACH_PORT_NULL;
	  *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	  strcpy (retry_name, pathbuf);
	  mach_port_deallocate (mach_task_self (), dotdot);
	  return 0;
	}
      else
	{
	  *do_retry = FS_RETRY_REAUTH;
	  *retry_port = dotdot;
	  *retry_port_type = MACH_MSG_TYPE_MOVE_SEND;
	  strcpy (retry_name, pathbuf);
	  return 0;
	}
    }

  if ((type == S_IFSOCK || type == S_IFBLK || type == S_IFCHR
      || type == S_IFIFO) && (flags & (O_READ|O_WRITE|O_EXEC)))
    {
      err = EOPNOTSUPP;
      goto out;
    }

  err = netfs_check_open_permissions (cred, netfs_root_node, flags, 0);
  if (err)
    goto out;

  flags &= ~OPENONLY_STATE_MODES;

  newpi = netfs_make_protid (netfs_make_peropen (netfs_root_node, flags,
						 &peropen_context),
			     cred);
  mach_port_deallocate (mach_task_self (), dotdot);

  *do_retry = FS_RETRY_NORMAL;
  *retry_port = ports_get_right (newpi);
  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
  retry_name[0] = '\0';
  ports_port_deref (newpi);

 out:
  if (err)
    iohelp_free_iouser (cred);
  pthread_mutex_unlock (&netfs_root_node->lock);
  return err;
}
