/* Node cache management for NFS client implementation
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include <netinet/in.h>

/* Hash table containing all the nodes currently active. */
#define CACHESIZE 512
static struct node *nodehash [CACHESIZE];

/* Compute and return a hash key for NFS file handle DATA of LEN bytes. */
static inline int
hash (int *data, size_t len)
{
  unsigned int h = 0;
  char *cp = (char *)data;
  int i;
  
  for (i = 0; i < len; i++)
    h += cp[i];
  
  return h % CACHESIZE;
}

/* Lookup the file handle in RPC result at P in the hash table.  If it
   is not present, initialize a new node structure and insert it into
   the hash table.  Whichever course, a new reference is generated and
   the node is returned in *NPP.  Return the address in the RPC result
   after the file handle.  */
int *
lookup_fhandle (int *p, struct node **npp)
{
  struct node *np;
  struct netnode *nn;
  size_t len;
  int h;

  if (protocol_version == 2)
    len = NFS2_FHSIZE;
  else
    len = ntohl (*p++);
  h = hash (p, len);

  spin_lock (&netfs_node_refcnt_lock);
  for (np = nodehash[h]; np; np = np->nn->hnext)
    {
      if (np->nn->handle.size != len
	  || bcmp (np->nn->handle.data, p, len) != 0)
	continue;
      
      np->references++;
      spin_unlock (&netfs_node_refcnt_lock);
      mutex_lock (&np->lock);
      *npp = np;
      return p + len / sizeof (int);
    }
  
  nn = malloc (sizeof (struct netnode));
  nn->handle.size = len;
  bcopy (p, nn->handle.data, len);
  nn->stat_updated = 0;
  nn->dtrans = NOT_POSSIBLE;
  nn->dead_dir = 0;
  nn->dead_name = 0;
  
  np = netfs_make_node (nn);
  mutex_lock (&np->lock);
  nn->hnext = nodehash[h];
  if (nn->hnext)
    nn->hnext->nn->hprevp = &nn->hnext;
  nn->hprevp = &nodehash[h];
  nodehash[h] = np;

  spin_unlock (&netfs_node_refcnt_lock);
  
  *npp = np;
  return p + len / sizeof (int);
}

/* Called by libnetfs when node NP has no more references.  (See
   <hurd/libnetfs.h> for details.  Just clear local state and remove
   from the hash table. */
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
      if (np->nn->dtrans == SYMLINK)
	free (np->nn->transarg.name);
      free (np->nn);
      free (np);
    }
}

/* Change the file handle used for node NP to be the handle at P.
   Make sure the hash table stays up to date.  Return the address
   after the hnadle. */
int *
recache_handle (int *p, struct node *np)
{
  int h;
  size_t len;

  if (protocol_version == 2)
    len = NFS2_FHSIZE;
  else
    len = ntohl (*p++);
  
  spin_lock (&netfs_node_refcnt_lock);
  *np->nn->hprevp = np->nn->hnext;
  if (np->nn->hnext)
    np->nn->hnext->nn->hprevp = np->nn->hprevp;

  np->nn->handle.size = len;
  bcopy (p, np->nn->handle.data, len);
  
  h = hash (p, len);
  np->nn->hnext = nodehash[h];
  if (np->nn->hnext)
    np->nn->hnext->nn->hprevp = &np->nn->hnext;
  np->nn->hprevp = &nodehash[h];
  
  spin_unlock (&netfs_node_refcnt_lock);
  return p + len / sizeof (int);
}


