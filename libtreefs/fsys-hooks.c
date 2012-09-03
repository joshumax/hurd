/* Default hooks for filesystems

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

#include "treefs.h"

/* Called to get the root node of the a filesystem, with a reference,
   returning it in ROOT, or return an error if it can't be had.  The default
   hook just returns FSYS->root or an error if it's NULL.  Note that despite
   the similar name, this is very different from fsys_s_getroot!  FSYS must
   not be locked.  */
error_t
_treefs_fsys_get_root (struct treefs_fsys *fsys, struct treefs_node **root)
{
  pthread_mutex_lock (&fsys->lock);
  *root = fsys->root;
  if (*root != NULL)
    treefs_node_ref (*root);
  pthread_mutex_unlock (&fsys->lock);

  return *root ? 0 : EOPNOTSUPP;
}

/* Called on a filesystem to create a new node in that filesystem, returning
   it in NODE.  DIR, if non-NULL, is the nominal parent directory, and MODE
   and CRED are the desired mode and user info respectively.  This hook
   should also call node_init_stat & node_init to initialize the various user
   bits.  */
error_t
_treefs_fsys_create_node (struct treefs_fsys *fsys,
			  struct treefs_node *dir,
			  mode_t mode, struct treefs_protid *cred,
			  struct treefs_node **node)
{
  error_t err = treefs_create_node (fsys, node);

  if (err)
    return err;

  err = treefs_node_init_stat (*node, dir, mode, cred);
  if (!err)
    err = treefs_node_init (*node, dir, mode, cred);
  if (S_ISDIR (mode))
    {
      treefs_dir_init (*node, dir, mode, cred);
      treefs_mdir_init (*node, dir, mode, cred);
    }
  if (err)
    {
      /* There shouldn't be any other state to free at this point --
	 node_init_stat shouldn't do more than init the stat structure, and
	 if node_init fails, it should clean up after itself.  */
      treefs_free_node (*node);
      return err;
    }

  return 0;
}

/* Called on a filesystem to destroy a node in that filesystem.  This call
   should *really* destroy it, i.e., it's only called once all references are
   gone.  */
void
_treefs_fsys_destroy_node (struct treefs_fsys *fsys, struct treefs_node *node)
{
  if (treefs_node_isdir (node))
    {
      treefs_mdir_finalize (node);
      treefs_dir_finalize (node);
    }
  treefs_node_finalize (node);
  treefs_free_node (node);
}
