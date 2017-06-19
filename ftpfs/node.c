/* General fs node functions

   Copyright (C) 1997, 2006 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <fcntl.h>
#include <string.h>

#include <hurd/ihash.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>
#include <hurd/netfs.h>

#include "ftpfs.h"
#include "ccache.h"

/* Node maintenance.  */

/* Return a new node in NODE, with a name NAME, and return the new node
   with a single reference in NODE.  E may be 0, if this is the root node.  */
error_t
ftpfs_create_node (struct ftpfs_dir_entry *e, const char *rmt_path,
		   struct node **node)
{
  struct node *new;
  struct netnode *nn = malloc (sizeof (struct netnode));
  error_t err;

  if (! nn)
    return ENOMEM;

  nn->fs = e->dir->fs;
  nn->dir_entry = e;
  nn->contents = 0;
  nn->dir = 0;
  nn->rmt_path = strdup (rmt_path);
  nn->ncache_next = nn->ncache_prev = 0;

  new = netfs_make_node (nn);
  if (! new)
    {
      free (nn);
      return ENOMEM;
    }

  fshelp_touch (&new->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		ftpfs_maptime);

  pthread_spin_lock (&nn->fs->inode_mappings_lock);
  err = hurd_ihash_add (&nn->fs->inode_mappings, e->stat.st_ino, e);
  pthread_spin_unlock (&nn->fs->inode_mappings_lock);

  if (err)
    {
      free (nn);
      free (new);
      return err;
    }

  e->node = new;
  *node = new;

  return 0;
}

/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  struct netnode *nn = node->nn;

  netfs_nref (node);

  /* Remove NODE from any entry it is attached to.  */
  ftpfs_detach_node (node);

  if (nn->dir)
    {
      assert_backtrace (nn->dir->num_live_entries == 0);
      ftpfs_dir_free (nn->dir);
    }

  /* Remove this entry from the set of known inodes.  */
  pthread_spin_lock (&nn->fs->inode_mappings_lock);
  hurd_ihash_locp_remove (&nn->fs->inode_mappings, nn->dir_entry->inode_locp);
  pthread_spin_unlock (&nn->fs->inode_mappings_lock);

  if (nn->contents)
    ccache_free (nn->contents);

  free (nn);
  free (node);
}
