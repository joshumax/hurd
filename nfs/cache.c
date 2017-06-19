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

/* Compute and return a hash key for NFS file handle.  */
static hurd_ihash_key_t
ihash_hash (const void *data)
{
  const struct fhandle *handle = (struct fhandle *) data;
  return (hurd_ihash_key_t) hurd_ihash_hash32 (handle->data, handle->size, 0);
}

/* Compare two handles which are used as keys.  */
static int
ihash_compare (const void *key1, const void *key2)
{
  const struct fhandle *handle1 = (struct fhandle *) key1;
  const struct fhandle *handle2 = (struct fhandle *) key2;

  return handle1->size == handle2->size &&
    memcmp (handle1->data, handle2->data, handle1->size) == 0;
}

/* Hash table containing all the nodes currently active.  */
static struct hurd_ihash nodehash =
  HURD_IHASH_INITIALIZER_GKI (sizeof (struct node)
                              + offsetof (struct netnode, slot), NULL, NULL,
                              ihash_hash, ihash_compare);

pthread_mutex_t nodehash_ihash_lock = PTHREAD_MUTEX_INITIALIZER;

/* Lookup the file handle HANDLE in the hash table.  If it is
   not present, initialize a new node structure and insert it into the
   hash table.  Whichever course, a new reference is generated and the
   node is returned in *NPP; the lock on the node, (*NPP)->LOCK, is
   held.  */
void
lookup_fhandle (struct fhandle *handle, struct node **npp)
{
  struct node *np;
  struct netnode *nn;

  pthread_mutex_lock (&nodehash_ihash_lock);
  np = hurd_ihash_find (&nodehash, (hurd_ihash_key_t) handle);
  if (np)
    {
      netfs_nref (np);
      pthread_mutex_unlock (&nodehash_ihash_lock);
      pthread_mutex_lock (&np->lock);
      *npp = np;
      return;
    }
  
  /* Could not find it */
  np = netfs_make_node_alloc (sizeof (struct netnode));
  assert_backtrace (np);
  nn = netfs_node_netnode (np);

  nn->handle.size = handle->size;
  memcpy (nn->handle.data, handle->data, handle->size);
  nn->stat_updated = 0;
  nn->dtrans = NOT_POSSIBLE;
  nn->dead_dir = 0;
  nn->dead_name = 0;
  
  hurd_ihash_add (&nodehash, (hurd_ihash_key_t) &nn->handle, np);
  netfs_nref_light (np);
  pthread_mutex_unlock (&nodehash_ihash_lock);
  pthread_mutex_lock (&np->lock);
  
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
   <hurd/libnetfs.h> for details.  */
void
netfs_node_norefs (struct node *np)
{
  if (np->nn->dead_dir)
    {
      struct fnd *args;
      pthread_t thread;
      error_t err;

      args = malloc (sizeof (struct fnd));
      assert_backtrace (args);

      args->dir = np->nn->dead_dir;
      args->name = np->nn->dead_name;
      np->nn->dead_dir = 0;
      np->nn->dead_name = 0;

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
    }
  else
    {
      if (np->nn->dtrans == SYMLINK)
        free (np->nn->transarg.name);
      free (np);
    }
}

/* When dropping soft refs, we simply remove the node from the
   node cache.  */
void
netfs_try_dropping_softrefs (struct node *np)
{
  pthread_mutex_lock (&nodehash_ihash_lock);
  hurd_ihash_locp_remove (&nodehash, np->nn->slot);
  netfs_nrele_light (np);
  pthread_mutex_unlock (&nodehash_ihash_lock);
}

/* Change the file handle used for node NP to be the handle at P.
   Make sure the hash table stays up to date.  Return the address
   after the handle.  The lock on the node should be held.  */
int *
recache_handle (int *p, struct node *np)
{
  size_t len;

  if (protocol_version == 2)
    len = NFS2_FHSIZE;
  else
    {
      len = ntohl (*p);
      p++;
    }
  
  /* Unlink it */
  pthread_mutex_lock (&nodehash_ihash_lock);
  hurd_ihash_locp_remove (&nodehash, np->nn->slot);

  /* Change the name */
  np->nn->handle.size = len;
  memcpy (np->nn->handle.data, p, len);
  
  /* Reinsert it */
  hurd_ihash_add (&nodehash, (hurd_ihash_key_t) &np->nn->handle, np);
  
  pthread_mutex_unlock (&nodehash_ihash_lock);
  return p + len / sizeof (int);
}
