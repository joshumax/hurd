/* Default hooks for directory nodes

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

#include <sys/fcntl.h>

#include "treefs.h"

/* ---------------------------------------------------------------- */

/* Return in CHILD a new node with one reference, presumably a possible child
   of DIR, with a mode MODE.  All attempts to create a new node go through
   this hook, so it may be overridden to easily control creation (e.g.,
   replacing it with a hook that always returns EPERM).  Note that this
   routine doesn't actually enter the child into the directory, or give the
   node a non-zero link count, that should be done by the caller.  */
error_t
_treefs_dir_create_child (struct treefs_node *dir, 
			   mode_t mode, struct treefs_auth *auth,
			   struct treefs_node **child)
{
  error_t err;

  if (!treefs_node_isdir (dir))
    return ENOTDIR;

  err = treefs_fsys_create_node (dir->fsys, dir, mode, auth, child);
  if (err)
    return err;

  return 0;
}

/* ---------------------------------------------------------------- */

/* Lookup NAME in DIR, returning the result in CHILD; AUTH should be used to
   do authentication.  FLAGS is the open flags; if FLAGS contains O_CREAT,
   and NAME is not found, then an entry should be created with a mode of
   CREATE_MODE (which includes the S_IFMT bits, e.g., S_IFREG means a normal
   file), unless O_EXCL is also set, in which case EEXIST should be returned.
   Possible special errors returned include: EAGAIN -- result would be the
   parent of our filesystem root.  */
error_t
_treefs_dir_lookup (struct treefs_node *dir, char *name,
		    struct treefs_auth *auth,
		    int flags, int create_mode,
		    struct treefs_node **child)
{
  error_t err;

  if (strcmp (name, "..") == 0 && dir == dir->fsys->root)
    /* Whoops, the result is above our heads.  */
    err = EAGAIN;
  else
    /* See if we've got an in-core entry for this file.  */
    err = treefs_mdir_get (dir, name, child);

  if (err == 0 && (flags & O_EXCL))
    return EEXIST;

  if (err == ENOENT)
    /* See if there's some other way of getting this file.  */
    err = treefs_dir_noent (dir, name, auth, flags, create_mode, child);

  if (err == ENOENT && (flags & O_CREAT))
    /* No such file, but the user wants to create it.  */
    {
      err = treefs_dir_create_child (dir, create_mode, auth, child);
      if (!err)
	{
	  err = treefs_dir_link (dir, name, *child, auth);
	  if (err)
	    treefs_node_unref (*child);
	}
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Link the node CHILD into DIR as NAME, using AUTH to check authentication.
   DIR should be locked and CHILD shouldn't be.  The default hook puts it
   into DIR's in-core directory, and uses a reference to CHILD.  */
error_t
_treefs_dir_link (struct treefs_node *dir, char *name,
		  struct treefs_node *child, struct treefs_auth *auth)
{
  struct treefs_node *old_child;
  error_t err = treefs_node_mod_link_count (child, 1);
  if (!err)
    {
      err = treefs_mdir_add (dir, name, child, &old_child);
      if (err)
	/* back out */
	treefs_node_mod_link_count (child, -1);
      else if (old_child)
	treefs_node_mod_link_count (old_child, -1);
    }
  return err;
}

/* Remove the entry NAME from DIR, using AUTH to check authentication.  DIR
   should be locked.  The default hook removes NAME from DIR's in-core
   directory.  */
error_t
_treefs_dir_unlink (struct treefs_node *dir, char *name,
		    struct treefs_auth *auth)
{
  struct treefs_node *old_child;
  error_t err = treefs_mdir_remove (dir, name, &old_child);
  if (!err && old_child)
    {
      treefs_node_mod_link_count (old_child, -1);
      treefs_node_unref (old_child);
    }
  return err;
}
