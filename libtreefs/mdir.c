/* Low-level directory manipulation functions

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

/* Low level dir management routines.  If called on a non-dir, ENOTDIR is
   returned.  */

/* Add CHILD to DIR as NAME, replacing any existing entry.  If OLD_CHILD is
   NULL, and NAME already exists in dir, EEXIST is returned, otherwise, any
   previous child is replaced and returned in OLD_CHILD.  DIR should be
   locked.  */
error_t
treefs_mdir_add (struct treefs_node *dir, char *name,
		 struct treefs_node *child, struct treefs_node **old_child)
{
  if (!treefs_node_isdir (dir))
    return ENOTDIR;
  else
    {
      if (dir->children == NULL)
	{
	  dir->children = treefs_make_node_list ();
	  if (dir->children == NULL)
	    return ENOMEM;
	}
      return treefs_node_list_add (dir->children, name, child, old_child);
    }
}

/* Remove any entry in DIR called NAME.  If there is no such entry, ENOENT is
   returned.  If OLD_CHILD is non-NULL, any removed entry is returned in it.
   DIR should be locked.  */
error_t
treefs_mdir_remove (struct treefs_node *dir, char *name,
		    struct treefs_node *old_child)
{
  if (!treefs_node_isdir (dir))
    return ENOTDIR;
  else if (dir->children == NULL)
    /* Normally this shouldn't happen.  */
    return ENOENT;
  else
    return treefs_node_list_remove (dir->children, name, old_child);
}

/* Returns in CHILD any entry called NAME in DIR, or NULL (and ENOENT) if
   there isn't such.  DIR should be locked.  */
error_t
treefs_mdir_get (struct treefs_node *dir, char *name,
		 struct treefs_node **child)
{
  if (!treefs_node_isdir (dir))
    return ENOTDIR;
  else if (dir->children == NULL)
    /* Normally this shouldn't happen.  */
    return ENOENT;
  else
    return treefs_node_list_get (dir->children, name, child);
}

/* Call FUN on each child of DIR; if FUN returns a non-zero value at any
   point, stop iterating and return that value immediately.  */
error_t
treefs_mdir_for_each (struct treefs_node *dir,
		     error_t (*fun)(char *name, struct treefs_node *child))
{
  if (!treefs_node_isdir (dir))
    return ENOTDIR;
  else if (dir->children == NULL)
    /* Normally this shouldn't happen.  */
    return 0;
  else
    return treefs_node_list_for_each (dir->children, fun);
}
