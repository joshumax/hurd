/* cache.c - Node cache management for NFS client implementation.
   Copyright (C) 1995, 1996, 1997, 2002 Free Software Foundation, Inc.
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
#include <stdio.h>
#include <netinet/in.h>

/* Hash table containing all the nodes currently active.  XXX Was 512,
   however, a prime is much nicer for the hash function.  509 is nice
   as not only is it prime, it also keeps the array within a page or
   two.  */
#define CACHESIZE 509
static struct node *nodehash [CACHESIZE];

/* Compute and return a hash key for NFS file handle DATA of LEN
   bytes.  */
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

/* Lookup the file handle P (length LEN) in the hash table.  If it is
   not present, initialize a new node structure and insert it into the
   hash table.  Whichever course, a new reference is generated and the
   node is returned in *NPP; the lock on the node, (*NPP)->LOCK, is
   held.  */
void
lookup_fhandle (void *p, size_t len, struct node **npp)
{
  struct node *np;
  struct netnode *nn;
  int h;

  h = hash (p, len);

  pthread_spin_lock (&netfs_node_refcnt_lock);
  for (np = nodehash[h]; np; np = np->nn->hnext)
    {
      if (np->nn->handle.size != len
	  || memcmp (np->nn->handle.data, p, len) != 0)
	continue;
      
      np->references++;
      pthread_spin_unlock (&netfs_node_refcnt_lock);
      pthread_mutex_lock (&np->lock);
      *npp = np;
      return;
    }
  
  /* Could not find it */
  nn = malloc (sizeof (struct netnode));
  assert (nn);

  nn->handle.size = len;
  memcpy (nn->handle.data, p, len);
  nn->stat_updated = 0;
  nn->dtrans = NOT_POSSIBLE;
  nn->dead_dir = 0;
  nn->dead_name = 0;
  
  np = netfs_make_node (nn);
  pthread_mutex_lock (&np->lock);
  nn->hnext = nodehash[h];
  if (nn->hnext)
    nn->hnext->nn->hprevp = &nn->hnext;
  nn->hprevp = &nodehash[h];
  nodehash[h] = np;

  pthread_spin_unlock (&netfs_node_refcnt_lock);
  
  *npp = np;
}

/* Package holding args to forked_node_delete.  */
struct fnd
{
  struct node *dir;
  char *name;
};

/* Worker function to delete nodes that don't have any more local
   references or links.  */
void *
forked_node_delete (void *arg)
{
  struct fnd *args = arg;
  
  pthread_mutex_lock (&args->dir->lock);
  netfs_attempt_unlink ((struct iouser *)-1, args->dir, args->name);
  netfs_nput (args->dir);
  free (args->name);
  free (args);
  return 0;
};

/* Called by libnetfs when node NP has no more references.  (See
   <hurd/libnetfs.h> for details.  Just clear its local state and
   remove it from the hash table.  Called and expected to leave with
   NETFS_NODE_REFCNT_LOCK held.  */
void
netfs_node_norefs (struct node *np)
{
  if (np->nn->dead_dir)
    {
      struct fnd *args;
      pthread_t thread;
      error_t err;

      args = malloc (sizeof (struct fnd));
      assert (args);

      np->references++;
      pthread_spin_unlock (&netfs_node_refcnt_lock);

      args->dir = np->nn->dead_dir;
      args->name = np->nn->dead_name;
      np->nn->dead_dir = 0;
      np->nn->dead_name = 0;
      netfs_nput (np);

      /* Do this in a separate thread so that we don't wait for it; it
	 acquires a lock on the dir, which we are not allowed to
	 do.  */
      err = pthread_create (&thread, NULL, forked_node_delete, args);
      if (!err)
	pthread_detach (thread);
      else
	{
	  errno = err;
	  perror ("pthread_create");
	}

      /* Caller expects us to leave this locked... */
      pthread_spin_lock (&netfs_node_refcnt_lock);
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
   after the handle.  The lock on the node should be held.  */
int *
recache_handle (int *p, struct node *np)
{
  int h;
  size_t len;

  if (protocol_version == 2)
    len = NFS2_FHSIZE;
  else
    {
      len = ntohl (*p);
      p++;
    }
  
  /* Unlink it */
  pthread_spin_lock (&netfs_node_refcnt_lock);
  *np->nn->hprevp = np->nn->hnext;
  if (np->nn->hnext)
    np->nn->hnext->nn->hprevp = np->nn->hprevp;

  /* Change the name */
  np->nn->handle.size = len;
  memcpy (np->nn->handle.data, p, len);
  
  /* Reinsert it */
  h = hash (p, len);
  np->nn->hnext = nodehash[h];
  if (np->nn->hnext)
    np->nn->hnext->nn->hprevp = &np->nn->hnext;
  np->nn->hprevp = &nodehash[h];
  
  pthread_spin_unlock (&netfs_node_refcnt_lock);
  return p + len / sizeof (int);
}

