/* Directory name lookup caching

   Copyright (C) 1996 Free Software Foundation, Inc.
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
#include <string.h>

/* Maximum number of names to cache at once */
#define MAXCACHE 256

/* Maximum length of file name we bother caching */
#define CACHE_NAME_LEN 100

/* Cache entry */
struct lookup_cache
{
  /* Used to indentify nodes to the fs dependent code.  0 for NODE_CACHE_ID
     means a `negative' entry -- recording that there's definitely no node with
     this name.  */
  int dir_cache_id, node_cache_id;

  /* Name of the node NODE_CACHE_ID in the directory DIR_CACHE_ID.  Entries
     with names too long to fit in this buffer aren't cached at all.  */
  char name[CACHE_NAME_LEN];

  /* Strlen of NAME.  If this is zero, it's an unused entry. */
  size_t name_len;		

  /* Next and prev entries in the cache, linked in LRU order.  */
  struct lookup_cache *next, *prev;
};

/* The contents of the cache in no particular order */
static struct lookup_cache lookup_cache[MAXCACHE];

/* The least, and most, recently used entries in the cache.  These point to
   either end of a linked list composed of all the elements of LOOKUP_CACHE.
   This list will always be the same length -- if an element is `removed',
   its entry is simply marked inactive, and moved to the LRU end of the list
   so it will be reused first.  */
static struct lookup_cache *lru_cache = 0, *mru_cache = 0;

static spin_lock_t cache_lock = SPIN_LOCK_INITIALIZER;

/* Buffer to hold statistics */
static struct
{
  long pos_hits;
  long neg_hits;
  long miss;
  long fetch_errors;
} statistics;

/* Move C to the most-recently-used end of the cache.  CACHE_LOCK must be
   held. */
static void
make_mru (struct lookup_cache *c)
{
  if (c != mru_cache)
    {
      /* First remove it.  We known C->prev isn't 0 because C wasn't
	 previously == MRU_CACHE.  */
      c->prev->next = c->next;
      if (c->next)
	c->next->prev = c->prev;
      else
	lru_cache = c->prev;

      /* Now make it MRU_CACHE.  */
      c->next = mru_cache;
      c->prev = 0;
      mru_cache->prev = c;
      mru_cache = c;
    }
}

/* Move C to the least-recently-used end of the cache.  CACHE_LOCK must be
   held. */
static void
make_lru (struct lookup_cache *c)
{
  if (c != lru_cache)
    {
      /* First remove it.  We known C->next isn't 0 because C wasn't
	 previously == LRU_CACHE.  */
      c->next->prev = c->prev;
      if (c->prev)
	c->prev->next = c->next;
      else
	mru_cache = c->next;

      /* Now make it LRU_CACHE.  */
      c->prev = lru_cache;
      c->next = 0;
      lru_cache->next = c;
      lru_cache = c;
    }
}

/* If there's an entry for NAME, of length NAME_LEN, in directory DIR in the
   cache, return it's entry, otherwise 0.  CACHE_LOCK must be held.  */
static struct lookup_cache *
find_cache (struct node *dir, const char *name, size_t name_len)
{
  struct lookup_cache *c;

  /* Search the list.  All unused entries are contiguous at the end of the
     list, so we can stop searching when we see the first one.  */
  for (c = mru_cache; c && c->name_len; c = c->next)
    if (c->name_len == name_len
	&& c->dir_cache_id == dir->cache_id
	&& c->name[0] == name[0] && strcmp (c->name, name) == 0)
      return c;

  return 0;
}

/* Put all the elements of the LOOKUP_CACHE array in a doubly linked list
   pointed to at either end by LRU_CACHE and MRU_CACHE, and initialize each
   entry.  */
static void
init_lookup_cache ()
{
  int i;

  lru_cache = mru_cache = &lookup_cache[0];

  lru_cache->name_len = 0;
  lru_cache->next = 0;

  for (i = 1; i < MAXCACHE; i++)
    {
      struct lookup_cache *c = &lookup_cache[i];
      c->name_len = 0;
      c->next = mru_cache;
      mru_cache->prev = c;
      mru_cache = c;
    }

  mru_cache->prev = 0;
}

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void
diskfs_enter_lookup_cache (struct node *dir, struct node *np, char *name)
{
  struct lookup_cache *c;
  size_t name_len = strlen (name);
  
  if (name_len > CACHE_NAME_LEN - 1)
    return;

  /* Never cache . or ..; it's too much trouble to get the locking
     order right.  */
  if (name[0] == '.' 
      && (name[1] == '\0'
	  || (name[1] == '.' && name[2] == '\0')))
    return;

  spin_lock (&cache_lock);

  if (lru_cache == 0)
    /* There should always be an lru_cache; this being zero means that the
       cache hasn't been initialized yet.  Do so.  */
    init_lookup_cache ();

  /* See if there's an old entry for NAME in DIR.  If not, replace the least
     recently used entry.  */
  c = find_cache (dir, name, name_len) ?: lru_cache;

  /* Fill C with the new entry.  */
  c->dir_cache_id = dir->cache_id;
  c->node_cache_id = np ? np->cache_id : 0;
  strcpy (c->name, name);
  c->name_len = name_len;

  /* Now C becomes the MRU entry!  */
  make_mru (c);

  spin_unlock (&cache_lock);
}

/* Purge all references in the cache to NP as a node inside 
   directory DP. */
void
diskfs_purge_lookup_cache (struct node *dp, struct node *np)
{
  struct lookup_cache *c, *next;
  
  spin_lock (&cache_lock);
  for (c = mru_cache; c; c = next)
    {
      /* Save C->next, since we may delete C from this position in the list. */
      next = c->next;

      if (c->name_len
	  && c->dir_cache_id == dp->cache_id
	  && c->node_cache_id == np->cache_id)
	{
	  c->name_len = 0;
	  make_lru (c);		/* Use C as the next free entry.  */
	}
    }
  spin_unlock (&cache_lock);
}

/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *
diskfs_check_lookup_cache (struct node *dir, char *name)
{
  struct lookup_cache *c;
  
  spin_lock (&cache_lock);

  c = find_cache (dir, name, strlen (name));
  if (c)
    {
      int id = c->node_cache_id;

      make_mru (c);		/* Record C as recently used.  */

      statistics.pos_hits++;
      spin_unlock (&cache_lock);

      if (id == 0)
	/* A negative cache entry.  */
	return (struct node *)-1;
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
	  error_t err = diskfs_cached_lookup (id, &np);
	  return err ? 0 : np;
	}
    }
  
  statistics.miss++;
  spin_unlock (&cache_lock);

  return 0;
}
