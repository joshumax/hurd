/*
   Copyright (C) 2018 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/* Data types required to create a directory tree using libnetfs */

#ifndef NETFS_IMPL_H
#define NETFS_IMPL_H

#include <hurd/netfs.h>

#include <acpifs.h>

/*
 * A netnode has a 1:1 relation with a generic libnetfs node. Hence, it's only
 * purpose is to extend a generic node to add the new attributes our problem
 * requires.
 */
struct netnode
{
  /* Light node */
  struct acpifs_dirent *ln;

  /* Position in the node cache.  */
  struct node *ncache_next, *ncache_prev;
};

#endif /* NETFS_IMPL_H */
