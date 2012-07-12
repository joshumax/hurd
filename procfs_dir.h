/* Hurd /proc filesystem, infrastructure for directories.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

/* This module provides an abstraction layer for implementing simple
   directories with (mostly) static contents.  The user defines the
   contents of the directory by providing a table of entries and various
   optional callback functions.  */

/* These operations define how a given entry will behave.  Either can be
   omitted, both from the entry-specific operations and from the
   directory-wide defaults.  */
struct procfs_dir_entry_ops
{
  /* Called when this entry is looked up to create a corresponding node.  */
  struct node *(*make_node)(void *dir_hook, const void *entry_hook);
  /* If this is provided and returns 0, this entry will be hidden.  */
  int (*exists)(void *dir_hook, const void *entry_hook);
};

/* Describes an individual directory entry, associating a NAME with
 * arbitrary HOOK data and node-specific OPS.  */
struct procfs_dir_entry
{
  const char *name;
  const void *hook;
  struct procfs_dir_entry_ops ops;
};

/* Describes a complete directory. ENTRIES is a table terminated by a
   null NAME field. ENTRY_OPS provides default operations for the
   entries which don't specify them.  The optional CLEANUP function
   should release all the resources associated with the directory hook.  */
struct procfs_dir_ops
{
  const struct procfs_dir_entry *entries;
  void (*cleanup)(void *dir_hook);
  struct procfs_dir_entry_ops entry_ops;
};

/* Create and return a new node for the directory described in OPS.
   The DIR_HOOK is passed the MAKE_NODE callback function of looked up
   entries, as well as to the CLEANUP callback when the node is
   destroyed.  If not enough memory can be allocated, OPS->CLEANUP is
   invoked immediately and NULL is returned.  */
struct node *
procfs_dir_make_node (const struct procfs_dir_ops *ops, void *dir_hook);

