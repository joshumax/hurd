/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "nfs.h"

#include <string.h>

#define CACHESIZE 512

static struct node *nodehash [CACHESIZE];

static inline int
hash (void *fhandle)
{
  unsigned int h = 0;
  int i;
  
  for (i = 0; i < NFS_FHSIZE; i++)
    h += ((char *)fhandle)[i];
  
  return h % CACHESIZE;
}

struct node *
lookup_fhandle (void *fhandle)
{
  struct node *np;
  struct netnode *nn;
  int h = hash (fhandle);

  spin_lock (&netfs_node_refcnt_lock);
  for (np = nodehash[h]; np; np = np->nn->hnext)
    {
      if (bcmp (np->nn->handle, fhandle, NFS_FHSIZE) != 0)
	continue;
      
      np->references++;
      spin_unlock (&netfs_node_refcnt_lock);
      mutex_lock (&np->lock);
      return np;
    }
  
  nn = malloc (sizeof (struct netnode));
  bcopy (fhandle, nn->handle, NFS_FHSIZE);
  nn->stat_updated = 0;
  
  np = netfs_make_node (nn);
  mutex_lock (&np->lock);
  nn->hnext = nodehash[h];
  if (nn->hnext)
    nn->hnext->nn->hprevp = &nn->hnext;
  nn->hprevp = &nodehash[h];

  spin_unlock (&netfs_node_refcnt_lock);
  
  return np;
}

void
netfs_node_norefs (struct node *np)
{
  if (np->nn->dead_dir)
    {
      struct node *dir;
      char *name;

      np->references++;
      spin_unlock (&netfs_node_refcnt_lock);

      dir = np->nn->dead_dir;
      name = np->nn->dead_name;
      np->nn->dead_dir = 0;
      np->nn->dead_name = 0;
      netfs_nput (np);

      mutex_lock (&dir->lock);
      netfs_attempt_unlink ((struct netcred *)-1, dir, name);

      netfs_nput (dir);
      free (name);

      /* Caller expects us to leave this locked... */
      spin_lock (&netfs_node_refcnt_lock);
    }
  else
    {
      *np->nn->hprevp = np->nn->hnext;
      if (np->nn->hnext)
	np->nn->hnext->nn->hprevp = np->nn->hprevp;
      free (np->nn);
      free (np);
    }
}
