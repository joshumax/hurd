/* 
   Copyright (C) 1995, 1996, 2000, 2015-2019 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "netfs.h"
#include <hurd/fshelp.h>

static struct node *
init_node (struct node *np, struct netnode *nn)
{
  np->nn = nn;

  pthread_mutex_init (&np->lock, NULL);
  refcounts_init (&np->refcounts, 1, 0);
  np->sockaddr = MACH_PORT_NULL;
  np->owner = 0;

  fshelp_transbox_init (&np->transbox, &np->lock, np);
  fshelp_rlock_init (&np->userlock);
  
  return np;
}

struct node *
netfs_make_node (struct netnode *nn)
{
  struct node *np = malloc (sizeof (struct node));
  if (! np)
    return NULL;

  return init_node (np, nn);
}

struct node *
netfs_make_node_alloc (size_t size)
{
  struct node *np = malloc (sizeof (struct node) + size);

  if (np == NULL)
    return NULL;

  return init_node (np, netfs_node_netnode (np));
}
