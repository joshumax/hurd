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

#include "treefs.h"

/* ---------------------------------------------------------------- */

/* Create a basic node, with one reference and no user-specific fields
   initialized, and return it in NODE */
error_t
treefs_create_node (struct treefs_fsys *fsys, struct treefs_node **node)
{
  struct treefs_node *n = malloc (sizeof (struct treefs_node));

  if (n == NULL)
    return ENOMEM;

  n->fsys = fsys;
  n->refs = 1;
  n->light_refs = 0;
  n->hooks = fsys->hooks;
  n->children = NULL;
  n->u = NULL;
  pthread_mutex_init (&n->lock, NULL);
  fshelp_init_trans_link (&n->active_trans);
  fshelp_lock_init (&n->lock_box);

  *node = n;
  return 0;
}

/* Immediately destroy NODE, with no user-finalization.  */
error_t
treefs_free_node (struct treefs_node *node)
{
  free (node);
}

/* ---------------------------------------------------------------- */

/* Returns a new filesystem in FSYS.  */
error_t
treefs_create_fsys (struct port_bucket *port_bucket,
		    treefs_hook_vector_t hook_overrides,
		    struct treefs_fsys **fsys)
{
  treefs_hook_vector_t hooks = treefs_default_hooks;

  if (hook_overrides)
    {
      hooks = treefs_hooks_clone (hooks);
      treefs_hooks_override (hooks,  hooks_overrides);
    }

  *fsys =
    ports_allocate_port (port_bucket, sizeof (struct trivfs_control),
			 treefs_fsys_port_class);
  if (*fsys == NULL)
    return ENOMEM;

  pthread_mutex_init (&(*fsys)->lock, NULL);
  (*fsys)->root = NULL;

  (*fsys)->underlying_port = MACH_PORT_NULL;
  memset (&(*fsys)->underlying_stat, 0, sizeof(struct stat));

  (*fsys)->flags = treefs_default_flags;
  (*fsys)->max_symlinks = treefs_default_max_symlinks;
  (*fsys)->sync_interval = treefs_default_sync_interval;

  (*fsys)->fs_type = treefs_default_fsys_type;
  (*fsys)->fs_id = getpid();

  (*fsys)->hooks = hooks;

  (*fsys)->port_bucket = port_bucket;
  (*fsys)->protid_ports_class = treefs_protid_port_class;

  (*fsys)->u = NULL;

  return 0;
}


void ACKACKACK()
{
  /* Create a fake root node to bootstrap the filesystem.  */
  err = treefs_create_node(*fsys, &fake_root);
  if (err)
    goto barf;

  /* Remember stat info for the node we're mounted on.  */
  memset (&(*fsys)->underlying_stat, 0, sizeof (struct stat));
  file_stat (realnode, &(*fsys)->underlying_stat);

  /* Note that it points to *FSYS, but *FSYS's root doesn't point to it...
     If the user wants this to be the real root, it's his responsility to
     initialize if further and install it.  */
  fake_root->fsys = *fsys;
  bcopy (&(*fsys)->underlying_stat, &fake_root->stat);
  err = treefs_fsys_init (fake_root);
  if (err)
    goto barf;

  /* See if the silly user has in fact done so.  */
  if ((*fsys)->root != fake_root)
    treefs_free_node (fake_root);
}
