/* Reparent a file

   Copyright (C) 1997,2002 Free Software Foundation

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "priv.h"
#include "fs_S.h"

error_t
diskfs_S_file_reparent (struct protid *cred, mach_port_t parent,
			   mach_port_t *new, mach_msg_type_name_t *new_type)
{
  error_t err;
  struct node *node;
  struct protid *new_cred;
  struct peropen *new_po;

  if (! cred)
    return EOPNOTSUPP;
  
  node = cred->po->np;

  pthread_mutex_lock (&node->lock);
  err = diskfs_make_peropen (node, cred->po->openstat, cred->po, &new_po);
  if (! err)
    {
      err = diskfs_create_protid (new_po, cred->user, &new_cred);
      if (err)
	diskfs_release_peropen (new_po);
    }
  pthread_mutex_unlock (&node->lock);

  if (! err)
    {
      /* Remove old shadow root state.  */
      if (new_cred->po->shadow_root && new_cred->po->shadow_root != node)
	diskfs_nrele (new_cred->po->shadow_root);
      if (new_cred->po->shadow_root_parent)
	mach_port_deallocate (mach_task_self (),
			      new_cred->po->shadow_root_parent);

      /* And install PARENT instead.  */
      new_cred->po->shadow_root = node;
      new_cred->po->shadow_root_parent = parent;

      *new = ports_get_right (new_cred);
      *new_type = MACH_MSG_TYPE_MAKE_SEND;

      ports_port_deref (new_cred);
    }

  return err;
}
