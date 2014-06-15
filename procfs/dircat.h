/* Hurd /proc filesystem, concatenation of two directories.
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

/* Append the contents of NUM_DIRS directories.  DIRS is an array of
   directory nodes.  One reference is consumed for each of them. If a
   memory allocation error occurs, or if one of the directories is a
   NULL pointer, the references are dropped immediately and NULL is
   returned.  The given DIRS array is duplicated and can therefore be
   allocated on the caller's stack.  Strange things will happen if some
   elements of DIRS have entries with the same name or if one of them is
   not a directory.  */
struct node *
dircat_make_node (struct node *const *dirs, int num_dirs);
