/* Default orphan list hooks for libdiskfs.
   Provides weak no-op implementations of the orphan list functions.
   Filesystems with an ext3-style orphan list (e.g. ext2fs) override these.

   Written by Milos Nikic.
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "diskfs.h"

/* Add inode NP to the orphan list.  Called when nlink drops to 0
   while the node is still held open (has hard references).
   NP must be locked.  The default implementation does nothing. */
void __attribute__((weak)) diskfs_orphan_add (struct node *np)
{
  /* Do nothing */
}

/* Remove inode NP from the orphan list.  Called when the inode is
   about to be permanently freed in diskfs_drop_node.
   NP must be locked.  The default implementation does nothing. */
void __attribute__((weak)) diskfs_orphan_del (struct node *np)
{
  /* Do nothing */
}
