/* Reparent a file

   Copyright (C) 1997, 2001 Free Software Foundation

   Written by Miles Bader <miles@gnu.org>

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

#include "fs_S.h"

error_t
netfs_S_file_reparent (struct protid *cred, mach_port_t parent,
		       mach_port_t *new_file, mach_msg_type_name_t *new_file_type)
{
  error_t err;
  struct node *node;
  struct protid *new_cred;
  struct iouser *user;

  if (! cred)
    return EOPNOTSUPP;
  
  err = iohelp_dup_iouser (&user, cred->user);
  if (err)
    return err;

  node = cred->po->np;

  pthread_mutex_lock (&node->lock);
  
  new_cred =
    netfs_make_protid (netfs_make_peropen (node, cred->po->openstat, cred->po),
		       user);
  pthread_mutex_unlock (&node->lock);

  if (new_cred)
    {
      /* Remove old shadow root state.  */
      if (new_cred->po->shadow_root && new_cred->po->shadow_root != node)
	{
	  pthread_mutex_lock (&new_cred->po->shadow_root->lock);
	  netfs_nput (new_cred->po->shadow_root);
	}
      if (new_cred->po->shadow_root_parent)
	mach_port_deallocate (mach_task_self (), new_cred->po->shadow_root_parent);

      /* And install PARENT instead.  */
      new_cred->po->shadow_root = node;
      new_cred->po->shadow_root_parent = parent;

      *new_file = ports_get_right (new_cred);
      *new_file_type = MACH_MSG_TYPE_MAKE_SEND;

      ports_port_deref (new_cred);

      return 0;
    }
  else
    return errno;
}
