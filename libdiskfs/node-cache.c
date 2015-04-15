/* Inode cache.

   Copyright (C) 1994-2015 Free Software Foundation, Inc.

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

#include "priv.h"

#define	INOHSZ	8192
#if	((INOHSZ&(INOHSZ-1)) == 0)
#define	INOHASH(ino)	((ino)&(INOHSZ-1))
#else
#define	INOHASH(ino)	(((unsigned)(ino))%INOHSZ)
#endif

/* The nodehash is a cache of nodes.

   Access to nodehash and nodehash_nr_items is protected by
   nodecache_lock.

   Every node in the nodehash carries a light reference.  When we are
   asked to give up that light reference, we reacquire our lock
   momentarily to check whether someone else reacquired a reference
   through the nodehash.  */
static struct node *nodehash[INOHSZ];
static size_t nodehash_nr_items;
static pthread_rwlock_t nodecache_lock = PTHREAD_RWLOCK_INITIALIZER;

/* Initialize the inode hash table. */
static void __attribute__ ((constructor))
nodecache_init ()
{
}

/* Lookup node with inode number INUM.  Returns NULL if the node is
   not found in the node cache.  */
static struct node *
lookup (ino_t inum)
{
  struct node *np;
  for (np = nodehash[INOHASH(inum)]; np; np = np->hnext)
    if (np->cache_id == inum)
      return np;
  return NULL;
}

/* Fetch inode INUM, set *NPP to the node structure;
   gain one user reference and lock the node.  */
error_t __attribute__ ((weak))
diskfs_cached_lookup (ino_t inum, struct node **npp)
{
  return diskfs_cached_lookup_context (inum, npp, NULL);
}

/* Fetch inode INUM, set *NPP to the node structure;
   gain one user reference and lock the node.  */
error_t
diskfs_cached_lookup_context (ino_t inum, struct node **npp,
			      struct lookup_context *ctx)
{
  error_t err;
  struct node *np, *tmp;

  pthread_rwlock_rdlock (&nodecache_lock);
  np = lookup (inum);
  if (np)
    goto gotit;
  pthread_rwlock_unlock (&nodecache_lock);

  err = diskfs_user_make_node (&np, ctx);
  if (err)
    return err;

  np->cache_id = inum;
  pthread_mutex_lock (&np->lock);

  /* Put NP in NODEHASH.  */
  pthread_rwlock_wrlock (&nodecache_lock);
  tmp = lookup (inum);
  if (tmp)
    {
      /* We lost a race.  */
      diskfs_nput (np);
      np = tmp;
      goto gotit;
    }

  np->hnext = nodehash[INOHASH(inum)];
  if (np->hnext)
    np->hnext->hprevp = &np->hnext;
  np->hprevp = &nodehash[INOHASH(inum)];
  nodehash[INOHASH(inum)] = np;
  diskfs_nref_light (np);
  nodehash_nr_items += 1;
  pthread_rwlock_unlock (&nodecache_lock);

  /* Get the contents of NP off disk.  */
  err = diskfs_user_read_node (np, ctx);
  if (err)
    return err;
  else
    {
      *npp = np;
      return 0;
    }

 gotit:
  diskfs_nref (np);
  pthread_rwlock_unlock (&nodecache_lock);
  pthread_mutex_lock (&np->lock);
  *npp = np;
  return 0;
}

/* Lookup node INUM (which must have a reference already) and return it
   without allocating any new references. */
struct node *
diskfs_cached_ifind (ino_t inum)
{
  struct node *np;

  pthread_rwlock_rdlock (&nodecache_lock);
  np = lookup (inum);
  pthread_rwlock_unlock (&nodecache_lock);

  assert (np);
  return np;
}

void __attribute__ ((weak))
diskfs_try_dropping_softrefs (struct node *np)
{
  pthread_rwlock_wrlock (&nodecache_lock);
  if (np->hprevp != NULL)
    {
      /* Check if someone reacquired a reference through the
	 nodehash.  */
      struct references result;
      refcounts_references (&np->refcounts, &result);

      if (result.hard > 0)
	{
	  /* A reference was reacquired through a hash table lookup.
	     It's fine, we didn't touch anything yet. */
	  pthread_rwlock_unlock (&nodecache_lock);
	  return;
	}

      *np->hprevp = np->hnext;
      if (np->hnext)
	np->hnext->hprevp = np->hprevp;
      np->hnext = NULL;
      np->hprevp = NULL;
      nodehash_nr_items -= 1;
      diskfs_nrele_light (np);
    }
  pthread_rwlock_unlock (&nodecache_lock);

  diskfs_user_try_dropping_softrefs (np);
}

/* For each active node, call FUN.  The node is to be locked around the call
   to FUN.  If FUN returns non-zero for any node, then immediately stop, and
   return that value.  */
error_t __attribute__ ((weak))
diskfs_node_iterate (error_t (*fun)(struct node *))
{
  error_t err = 0;
  int n;
  size_t num_nodes;
  struct node *node, **node_list, **p;

  pthread_rwlock_rdlock (&nodecache_lock);

  /* We must copy everything from the hash table into another data structure
     to avoid running into any problems with the hash-table being modified
     during processing (normally we delegate access to hash-table with
     nodecache_lock, but we can't hold this while locking the
     individual node locks).  */
  /* XXX: Can we?  */
  num_nodes = nodehash_nr_items;

  /* TODO This method doesn't scale beyond a few dozen nodes and should be
     replaced.  */
  node_list = malloc (num_nodes * sizeof (struct node *));
  if (node_list == NULL)
    {
      pthread_rwlock_unlock (&nodecache_lock);
      error (0, 0, "unable to allocate temporary node table");
      return ENOMEM;
    }

  p = node_list;
  for (n = 0; n < INOHSZ; n++)
    for (node = nodehash[n]; node; node = node->hnext)
      {
	*p++ = node;

	/* We acquire a hard reference for node, but without using
	   diskfs_nref.	 We do this so that diskfs_new_hardrefs will not
	   get called.	*/
	refcounts_ref (&node->refcounts, NULL);
      }

  pthread_rwlock_unlock (&nodecache_lock);

  p = node_list;
  while (num_nodes-- > 0)
    {
      node = *p++;
      if (!err)
	{
	  pthread_mutex_lock (&node->lock);
	  err = (*fun)(node);
	  pthread_mutex_unlock (&node->lock);
	}
      diskfs_nrele (node);
    }

  free (node_list);
  return err;
}

/* The user must define this function if she wants to use the node
   cache.  Create and initialize a node.  */
error_t __attribute__ ((weak))
diskfs_user_make_node (struct node **npp, struct lookup_context *ctx)
{
  assert (! "diskfs_user_make_node not implemented");
}

/* The user must define this function if she wants to use the node
   cache.  Read stat information out of the on-disk node.  */
error_t __attribute__ ((weak))
diskfs_user_read_node (struct node *np, struct lookup_context *ctx)
{
  assert (! "diskfs_user_read_node not implemented");
}

/* The user must define this function if she wants to use the node
   cache.  The last hard reference to a node has gone away; arrange to
   have all the weak references dropped that can be.  */
void __attribute__ ((weak))
diskfs_user_try_dropping_softrefs (struct node *np)
{
  assert (! "diskfs_user_try_dropping_softrefs not implemented");
}
