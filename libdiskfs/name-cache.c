/* Directory name lookup caching
   Copyright (C) 1996 Free Software Foundation, Inc.
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


#include "priv.h"
#include <string.h>

/* Maximum number of names to cache at once */
#define MAXCACHE 256

/* Maximum length of file name we bother caching */
#define CACHE_NAME_LEN 100

/* Cache entry */
struct lookup_cache
{
  int dir_cache_id, node_cache_id;

  size_t len;		/* strlen of name */

  char name[CACHE_NAME_LEN];
};

/* The contents of the cache in no particular order */
static struct lookup_cache lookup_cache[MAXCACHE];

/* A rotating buffer of cache entries, maintaining an LRU
   ordering of lookup_cache. */
static int cache_indexes[MAXCACHE];

/* The current live entries start at first_cache and run through to
   last_cache.  When the cache is full, first_cache == last_cache,
   and these point at the lru element; lower indexes in cache_indexes
   are successively less recently used.  */
static int first_cache, last_cache;

static spin_lock_t cache_lock = SPIN_LOCK_INITIALIZER;

/* Buffer to hold statistics */
static struct
{
  long pos_hits;
  long neg_hits;
  long miss;
  long fetch_errors;
} statistics;

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void
diskfs_enter_lookup_cache (struct node *dir, struct node *np, char *name)
{
  size_t len = strlen (name);
  struct lookup_cache *lc;
  
  if (len > CACHE_NAME_LEN - 1)
    return;

  spin_lock (&cache_lock);

  /* Take first_cache + 1 and fill it with the new entry */
  lc = &lookup_cache[cache_indexes[first_cache + 1]];
  lc->dir_cache_id = dir->cache_id;
  lc->node_cache_id = np->cache_id;
  lc->len = len;
  strcpy (lc->name, name);
  
  /* Increment first_cache, and possibly last_cache too. */
  if (last_cache == first_cache)
    last_cache++;
  first_cache++;
  
  spin_unlock (&cache_lock);
}

/* Purge all references in the cache to NP as a node inside 
   directory DP. */
void
diskfs_purge_lookup_cache (struct node *dp, struct node *np)
{
  int i;
  
  spin_lock (&cache_lock);
  for (i = 0; i < MAXCACHE; i++)
    if (lookup_cache[i].len 
	&& lookup_cache[i].dir_cache_id == dp->cache_id
	&& lookup_cache[i].node_cache_id == np->cache_id)
      lookup_cache[i].len = 0;
  spin_unlock (&cache_lock);
}

/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *
diskfs_check_lookup_cache (struct node *dir, char *name)
{
  int i;
  size_t len = strlen (name);

  spin_lock (&cache_lock);

  /* Maybe we really ought to look up in lru order...who knows. */

  for (i = 0; i < MAXCACHE; i++)
    if (lookup_cache[i].len == len
	&& lookup_cache[i].dir_cache_id == dir->cache_id
	&& lookup_cache[i].name[0] == name[0]
	&& !strcmp (lookup_cache[i].name, name))
      {
	int id = lookup_cache[i].node_cache_id;

	statistics.pos_hits++;
	spin_unlock (&cache_lock);

	if (id == dir->cache_id)
	  /* The cached node is the same as DIR.  */
	  {
	    diskfs_nref (dir);
	    return dir;
	  }
	else
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
