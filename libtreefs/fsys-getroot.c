/*

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <fcntl.h>

#include <hurd/fsys.h>

#include "treefs.h"

error_t
_treefs_s_fsys_getroot (struct treefs_fsys *fsys,
			mach_port_t dotdot,
			uid_t *uids, unsigned nuids,
			uid_t *gids, unsigned ngids,
			int flags, retry_type *retry, char *retry_name,
			file_t *result, mach_msg_type_name_t *result_type)
{
  error_t err;
  mode_t type;
  struct treefs_node *root;
  struct treefs_auth *auth;

  flags &= O_HURD;

  err = treefs_fsys_get_root (fsys, &root);
  if (err)
    return err;

  if (!(flags & O_NOTRANS))
    /* Try starting up any translator on the root node.  */
    {
      fsys_t child_fsys;

      do
	{
	  err =
	    treefs_node_get_active_trans (root, 0, 0, &dotdot, &child_fsys);
	  if (err == 0 && child_fsys != MACH_PORT_NULL)
	    /* We think there's an active translator; try contacting it.  */
	    {
	      err =
		fsys_getroot (child_fsys, dotdot, MACH_MSG_TYPE_COPY_SEND,
			      uids, nuids, gids, ngids,
			      flags, retry, retry_name, result);
	      /* If we got MACH_SEND_INVALID_DEST or MIG_SERVER_DIED, then
		 the server is dead.  Zero out the old control port and try
		 everything again.  */
	      if (err == MACH_SEND_INVALID_DEST || err == EMIG_SERVER_DIED)
		treefs_node_drop_active_trans (root, control_port);
	    }
	}
      while (err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED);

      /* If we got a translator, or an error trying, return immediately.  */
      if (err || child_fsys)
	{
	  if (!err && *result != MACH_PORT_NULL)
	    *result_type = MACH_MSG_TYPE_MOVE_SEND;
	  else
	    *result_type = MACH_MSG_TYPE_COPY_SEND;

	  if (!err)
	    mach_port_deallocate (mach_task_self (), dotdot);
	  treefs_node_unref (root);

	  return err;
	}
    }

  pthread_mutex_lock (&root->lock);

  type = treefs_node_type (root);
  if (type == S_IFLNK && !(flags & (O_NOLINK | O_NOTRANS)))
    /* Handle symlink interpretation */
    {
      int sym_len = 1000;
      char path_buf[sym_len + 1], *path = path_buf;

      err = treefs_node_get_symlink (root, path, &sym_len);
      if (err == E2BIG)
	/* Symlink contents won't fit in our buffer, so
	   reallocate it and try again.  */
	{
	  path = alloca (sym_len + 1);
	  err = treefs_node_get_symlink (node, path, &sym_len);
	}

      if (err)
	goto out;
      
      if (*path == '/')
	{
	  *retry = FS_RETRY_MAGICAL;
	  *result = MACH_PORT_NULL;
	  *result_type = MACH_MSG_TYPE_COPY_SEND;
	  mach_port_deallocate (mach_task_self (), dotdot);
	}
      else
	{
	  *retry = FS_RETRY_REAUTH;
	  *result = dotdot;
	  *result_type = MACH_MSG_TYPE_COPY_SEND;
	}

      strcpy (retry_name, path);
      goto out;
    }
  
  err = treefs_node_create_auth (root, uids, nuids, gids, ngids, &auth);
  if (err)
    goto out;

  *retry = FS_RETRY_NORMAL;
  *retry_name = '\0';
  *result_type = MACH_MSG_TYPE_MAKE_SEND;

  err = treefs_node_create_right (root, dotdot, flags, auth, result);

  treefs_node_auth_unref (root, auth);

 out:
  treefs_node_release (root);

  return err;
}
