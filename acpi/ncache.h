/*
   Copyright (C) 2017 Free Software Foundation, Inc.

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

/* Header for node caching functions */

#ifndef NCACHE_H
#define NCACHE_H

#include <hurd/netfs.h>

#include <acpifs.h>

void node_cache (struct node *node);
void node_unlink (struct node *node, struct acpifs *fs);

#endif /* NCACHE_H */
