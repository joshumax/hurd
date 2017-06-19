/* Directory name lookup caching

   Copyright (C) 1996, 1997, 1998, 2014 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG, & Miles Bader.

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

#include "priv.h"
#include <assert-backtrace.h>
#include <hurd/ihash.h>
#include <string.h>

/* The name cache is implemented using a hash table.

   We use buckets of a fixed size.  We approximate the
   least-frequently used cache algorithm by counting the number of
   lookups using saturating arithmetic in the two lowest bits of the
   pointer to the name.  Using this strategy we achieve a constant
   worst-case lookup and insertion time.  */

/* Number of buckets.  Must be a power of two. */
#define CACHE_SIZE	256

/* Entries per bucket.  */
#define BUCKET_SIZE	4

/* A mask for fast binary modulo.  */
#define CACHE_MASK	(CACHE_SIZE - 1)

/* Cache bucket with BUCKET_SIZE entries.

   The layout of the bucket is chosen so that it will be straight
   forward to use vector operations in the future.  */
struct cache_bucket
{
  /* Name of the node NODE_CACHE_ID in the directory DIR_CACHE_ID.  If
     NULL, the entry is unused.  */
  unsigned long name[BUCKET_SIZE];

  /* The key.  */
  unsigned long key[BUCKET_SIZE];

  /* Used to indentify nodes to the fs dependent code.  */
  ino64_t dir_cache_id[BUCKET_SIZE];

  /* 0 for NODE_CACHE_ID means a `negative' entry -- recording that
     there's definitely no node with this name.  */
  ino64_t node_cache_id[BUCKET_SIZE];
};

/* The cache.  */
static struct cache_bucket name_cache[CACHE_SIZE];

/* Protected by this lock.  */
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* Given VALUE, return the char pointer.  */
static inline char *
charp (unsigned long value)
{
  return (char *) (value & ~3L);
}

/* Given VALUE, return the approximation of use frequency.  */
static inline unsigned long
frequ (unsigned long value)
{
  return value & 3;
}

/* Add an entry in the Ith slot of the given bucket.  If there is a
   value there, remove it first.  */
static inline void
add_entry (struct cache_bucket *b, int i,
	   const char *name, unsigned long key,
	   ino64_t dir_cache_id, ino64_t node_cache_id)
{
  if (b->name[i])
    free (charp (b->name[i]));

  b->name[i] = (unsigned long) strdup (name);
  assert_backtrace ((b->name[i] & 3) == 0);
  if (b->name[i] == 0)
    return;

  b->key[i] = key;
  b->dir_cache_id[i] = dir_cache_id;
  b->node_cache_id[i] = node_cache_id;
}

/* Remove the entry in the Ith slot of the given bucket.  */
static inline void
remove_entry (struct cache_bucket *b, int i)
{
  if (b->name[i])
    free (charp (b->name[i]));
  b->name[i] = 0;
}

/* Check if the entry in the Ith slot of the given bucket is
   valid.  */
static inline int
valid_entry (struct cache_bucket *b, int i)
{
  return b->name[i] != 0;
}

/* If there is no best candidate to replace, pick any.  We approximate
   any by picking the slot depicted by REPLACE, and increment REPLACE
   then.  */
static int replace;

/* Lookup (DIR_CACHE_ID, NAME, KEY) in the cache.  If it is found,
   return 1 and set BUCKET and INDEX to the item.  Otherwise, return 0
   and set BUCKET and INDEX to the slot where the item should be
   inserted.  */
static inline int
lookup (ino64_t dir_cache_id, const char *name, unsigned long key,
	struct cache_bucket **bucket, int *index)
{
  struct cache_bucket *b = *bucket = &name_cache[key & CACHE_MASK];
  unsigned long best = 3;
  int i;

  for (i = 0; i < BUCKET_SIZE; i++)
    {
      unsigned long f = frequ (b->name[i]);

      if (valid_entry (b, i)
	  && b->key[i] == key
	  && b->dir_cache_id[i] == dir_cache_id
	  && strcmp (charp (b->name[i]), name) == 0)
	{
	  if (f < 3)
	    b->name[i] += 1;

	  *index = i;
	  return 1;
	}

      /* Keep track of the replacement candidate.  */
      if (f < best)
	{
	  best = f;
	  *index = i;
	}
    }

  /* If there was no entry with a lower use frequency, just replace
     any entry.  */
  if (best == 3)
    {
      *index = replace;
      replace = (replace + 1) & (BUCKET_SIZE - 1);
    }

  return 0;
}

/* Hash the directory cache_id and the name.  */
static inline unsigned long
hash (ino64_t dir_cache_id, const char *name)
{
  unsigned long h;
  h = hurd_ihash_hash32 (&dir_cache_id, sizeof dir_cache_id, 0);
  h = hurd_ihash_hash32 (name, strlen (name), h);
  return h;
}

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void
diskfs_enter_lookup_cache (struct node *dir, struct node *np, const char *name)
{
  unsigned long key = hash (dir->cache_id, name);
  ino64_t value = np ? np->cache_id : 0;
  struct cache_bucket *bucket;
  int i = 0, found;

  pthread_mutex_lock (&cache_lock);
  found = lookup (dir->cache_id, name, key, &bucket, &i);
  if (! found)
    add_entry (bucket, i, name, key, dir->cache_id, value);
  else
    if (bucket->node_cache_id[i] != value)
      bucket->node_cache_id[i] = value;

  pthread_mutex_unlock (&cache_lock);
}

/* Purge all references in the cache to NP as a node inside
   directory DP. */
void
diskfs_purge_lookup_cache (struct node *dp, struct node *np)
{
  int i;
  struct cache_bucket *b;

  pthread_mutex_lock (&cache_lock);

  for (b = &name_cache[0]; b < &name_cache[CACHE_SIZE]; b++)
    for (i = 0; i < BUCKET_SIZE; i++)
      if (valid_entry (b, i)
	  && b->dir_cache_id[i] == dp->cache_id
	  && b->node_cache_id[i] == np->cache_id)
	remove_entry (b, i);

  pthread_mutex_unlock (&cache_lock);
}

/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *
diskfs_check_lookup_cache (struct node *dir, const char *name)
{
  unsigned long key = hash (dir->cache_id, name);
  int lookup_parent = name[0] == '.' && name[1] == '.' && name[2] == '\0';
  struct cache_bucket *bucket;
  int i, found;

  if (lookup_parent && dir == diskfs_root_node)
    /* This is outside our file system, return cache miss.  */
    return NULL;

  pthread_mutex_lock (&cache_lock);
  found = lookup (dir->cache_id, name, key, &bucket, &i);
  if (found)
    {
      ino64_t id = bucket->node_cache_id[i];
      pthread_mutex_unlock (&cache_lock);

      if (id == 0)
	/* A negative cache entry.  */
	return (struct node *) -1;
      else if (id == dir->cache_id)
	/* The cached node is the same as DIR.  */
	{
	  diskfs_nref (dir);
	  return dir;
	}
      else
	/* Just a normal entry in DIR; get the actual node.  */
	{
	  struct node *np;
	  error_t err;

	  if (lookup_parent)
	    {
	      pthread_mutex_unlock (&dir->lock);
	      err = diskfs_cached_lookup (id, &np);
	      pthread_mutex_lock (&dir->lock);

	      /* In the window where DP was unlocked, we might
		 have lost.  So check the cache again, and see
		 if it's still there; if so, then we win. */
	      pthread_mutex_lock (&cache_lock);
	      found = lookup (dir->cache_id, name, key, &bucket, &i);
	      if (! found
		  || bucket->node_cache_id[i] != id)
		{
		  pthread_mutex_unlock (&cache_lock);

		  /* Lose */
		  diskfs_nput (np);
		  return 0;
		}
	      pthread_mutex_unlock (&cache_lock);
	    }
	  else
	    err = diskfs_cached_lookup (id, &np);
	  return err ? 0 : np;
	}
    }

  pthread_mutex_unlock (&cache_lock);
  return 0;
}
