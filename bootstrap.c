/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   bootstrap.c -- This file is functions for starting up 
                  and initializers for the procfs translator
                  defined in procfs.h
               
   Copyright (C) 2008, FSF.
   Written as a Summer of Code Project
   
   procfs is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   procfs is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. 
*/

#include <stddef.h>
#include <hurd/ihash.h>
#include <hurd/netfs.h>

#include "procfs.h"

struct ps_context *ps_context;

/* This function is used to initialize the whole translator, can be
   effect called as bootstrapping the translator. */
error_t procfs_init ()
{
  error_t err;
  
  err = ps_context_create (getproc (), &ps_context);
  
  return err; 
}

/* Create a new procfs filesystem.  */
error_t procfs_create (char *procfs_root, int fsid,
                       struct procfs **fs)
{
  error_t err;
  /* This is the enclosing directory for this filesystem's
     root node  */
  struct procfs_dir *topmost_root_dir;
 
  /* And also a topmost-root node, just used for locking
     TOPMOST_ROOT_DIR.  */
  struct node *topmost_root;
  
  /* The new node for the filesystem's root.  */
  struct procfs *new = malloc (sizeof (struct procfs));

  if (! new)
    return ENOMEM;

  new->fsid = fsid;
  new->next_inode = 2;

  hurd_ihash_init (&new->inode_mappings,
		   offsetof (struct procfs_dir_entry, inode_locp));
  spin_lock_init (&new->inode_mappings_lock);

  topmost_root = netfs_make_node (0);
  if (! topmost_root)
    err = ENOMEM;
  else
    {
      err = procfs_dir_create (new, topmost_root, procfs_root,
                               &topmost_root_dir);
      if (! err)
        {
          /* ADDITIONAL BOOTSTRAPPING OF THE ROOT NODE */
	  err = procfs_dir_null_lookup (topmost_root_dir, &new->root);   
        }
    }

  if (err)
    {
      hurd_ihash_destroy (&new->inode_mappings);
      free (new);
    }
  else
    *fs = new;

  return err;
}

