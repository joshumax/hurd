/* Functions for dealing with node lists

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
struct treefs_node_list
{
  unsigned short num_nodes, nodes_alloced;
  
};

/* Return a new node list, or NULL if a memory allocation error occurs.  */
struct treefs_node_list *
treefs_make_node_list ()
{
  struct treefs_node_list *nl = malloc (sizeof (struct treefs_node_list));
  if (!nl)
    return NULL;

  nl->nodes_alloced = 0;
  nl->num_nodes = 0;

  return nl;
}

/* Add NODE to LIST as NAME, replacing any existing entry.  If OLD_NODE is
   NULL, and an entry NAME already exists, EEXIST is returned, otherwise, any
   previous child is replaced and returned in OLD_NODE.  */
error_t
treefs_node_list_add (struct treefs_node_list *list, char *name,
		      struct treefs_node *node, struct treefs_node **old_node)
{
  
}

/* Remove any entry in LIST called NAME.  If there is no such entry, ENOENT is
   returned.  If OLD_NODE is non-NULL, any removed entry is returned in it.  */
error_t
treefs_node_list_remove (struct treefs_node_list *list, char *name,
			 struct treefs_node **old_node)
{
}

/* Returns in NODE any entry called NAME in LIST, or NULL (and ENOENT) if
   there isn't such.  */
error_t
treefs_node_list_get (struct treefs_node_list *list, char *name,
		      struct treefs_node **node)
{
}
