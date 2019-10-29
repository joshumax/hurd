/*
   Copyright (C) 1994-1996, 2002, 2014-2019 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "priv.h"
#include <fcntl.h>


static struct node *
init_node (struct node *np, struct disknode *dn)
{
  np->dn = dn;
  np->dn_set_ctime = 0;
  np->dn_set_atime = 0;
  np->dn_set_mtime = 0;
  np->dn_stat_dirty = 0;
  np->author_tracks_uid = 0;

  pthread_mutex_init (&np->lock, NULL);
  refcounts_init (&np->refcounts, 1, 0);
  np->owner = 0;
  np->sockaddr = MACH_PORT_NULL;

  np->dirmod_reqs = 0;
  np->dirmod_tick = 0;
  np->filemod_reqs = 0;
  np->filemod_tick = 0;

  fshelp_transbox_init (&np->transbox, &np->lock, np);
  iohelp_initialize_conch (&np->conch, &np->lock);
  fshelp_rlock_init (&np->userlock);

  return np;
}

/* Create a and return new node structure with DN as its physical disknode.
   The node will have one hard reference and no light references.  */
struct node *
diskfs_make_node (struct disknode *dn)
{
  struct node *np = malloc (sizeof (struct node));

  if (np == 0)
    return 0;

  return init_node (np, dn);
}

/* Create a new node structure.  Also allocate SIZE bytes for the
   disknode.  The address of the disknode can be obtained using
   diskfs_node_disknode.  The new node will have one hard reference
   and no light references.  */
struct node *
diskfs_make_node_alloc (size_t size)
{
  struct node *np = malloc (sizeof (struct node) + size);

  if (np == NULL)
    return NULL;

  return init_node (np, diskfs_node_disknode (np));
}
