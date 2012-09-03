/* Directory name lookup caching

   Copyright (C) 1996, 1997, 1998 Free Software Foundation, Inc.
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
#include <cacheq.h>

/* Maximum number of names to cache at once */
#define MAXCACHE 200

/* Maximum length of file name we bother caching */
#define CACHE_NAME_LEN 100

/* Cache entry */
struct lookup_cache
{
  struct cacheq_hdr hdr;

  /* Used to indentify nodes to the fs dependent code.  0 for NODE_CACHE_ID
     means a `negative' entry -- recording that there's definitely no node with
     this name.  */
  int dir_cache_id, node_cache_id;

  /* Name of the node NODE_CACHE_ID in the directory DIR_CACHE_ID.  Entries
     with names too long to fit in this buffer aren't cached at all.  */
  char name[CACHE_NAME_LEN];

  /* Strlen of NAME.  If this is zero, it's an unused entry. */
  size_t name_len;

  /* XXX */
  int stati;
};

/* The contents of the cache in no particular order */
static struct cacheq lookup_cache = { sizeof (struct lookup_cache) };

static pthread_spinlock_t cache_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* Buffer to hold statistics */
static struct stats
{
  long pos_hits;
  long neg_hits;
  long miss;
  long fetch_errors;
} statistics;

#define PARTIAL_THRESH 100
#define NPARTIALS MAXCACHE / PARTIAL_THRESH
struct stats partial_stats [NPARTIALS];


/* If there's an entry for NAME, of length NAME_LEN, in directory DIR in the
   cache, return its entry, otherwise 0.  CACHE_LOCK must be held.  */
static struct lookup_cache *
find_cache (struct node *dir, const char *name, size_t name_len)
{
  struct lookup_cache *c;
  int i;

  /* Search the list.  All unused entries are contiguous at the end of the
     list, so we can stop searching when we see the first one.  */
  for (i = 0, c = lookup_cache.mru;
       c && c->name_len;
       c = c->hdr.next, i++)
    if (c->name_len == name_len
	&& c->dir_cache_id == dir->cache_id
	&& c->name[0] == name[0] && strcmp (c->name, name) == 0)
      {
	c->stati = i / 100;
	return c;
      }

  return 0;
}

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void
diskfs_enter_lookup_cache (struct node *dir, struct node *np, const char *name)
{
  struct lookup_cache *c;
  size_t name_len = strlen (name);

  if (name_len > CACHE_NAME_LEN - 1)
    return;

  pthread_spin_lock (&cache_lock);

  if (lookup_cache.length == 0)
    /* There should always be an lru_cache; this being zero means that the
       cache hasn't been initialized yet.  Do so.  */
    cacheq_set_length (&lookup_cache, MAXCACHE);

  /* See if there's an old entry for NAME in DIR.  If not, replace the least
     recently used entry.  */
  c = find_cache (dir, name, name_len) ?: lookup_cache.lru;

  /* Fill C with the new entry.  */
  c->dir_cache_id = dir->cache_id;
  c->node_cache_id = np ? np->cache_id : 0;
  strcpy (c->name, name);
  c->name_len = name_len;

  /* Now C becomes the MRU entry!  */
  cacheq_make_mru (&lookup_cache, c);

  pthread_spin_unlock (&cache_lock);
}

/* Purge all references in the cache to NP as a node inside
   directory DP. */
void
diskfs_purge_lookup_cache (struct node *dp, struct node *np)
{
  struct lookup_cache *c, *next;

  pthread_spin_lock (&cache_lock);
  for (c = lookup_cache.mru; c; c = next)
    {
      /* Save C->hdr.next, since we may move C from this position. */
      next = c->hdr.next;

      if (c->name_len
	  && c->dir_cache_id == dp->cache_id
	  && c->node_cache_id == np->cache_id)
	{
	  c->name_len = 0;
	  cacheq_make_lru (&lookup_cache, c); /* Use C as the next free
						 entry. */
	}
    }
  pthread_spin_unlock (&cache_lock);
}

/* Register a negative hit for an entry in the Nth stat class */
void
register_neg_hit (int n)
{
  int i;

  statistics.neg_hits++;

  for (i = 0; i < n; i++)
    partial_stats[i].miss++;
  for (; i < NPARTIALS; i++)
    partial_stats[i].neg_hits++;
}

/* Register a positive hit for an entry in the Nth stat class */
void
register_pos_hit (int n)
{
  int i;

  statistics.pos_hits++;

  for (i = 0; i < n; i++)
    partial_stats[i].miss++;
  for (; i < NPARTIALS; i++)
    partial_stats[i].pos_hits++;
}

/* Register a miss */
void
register_miss ()
{
  int i;

  statistics.miss++;
  for (i = 0; i < NPARTIALS; i++)
    partial_stats[i].miss++;
}



/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *
diskfs_check_lookup_cache (struct node *dir, const char *name)
{
  struct lookup_cache *c;

  pthread_spin_lock (&cache_lock);

  c = find_cache (dir, name, strlen (name));
  if (c)
    {
      int id = c->node_cache_id;

      cacheq_make_mru (&lookup_cache, c); /* Record C as recently used.  */

      if (id == 0)
	/* A negative cache entry.  */
	{
	  register_neg_hit (c->stati);
	  pthread_spin_unlock (&cache_lock);
	  return (struct node *)-1;
	}
      else if (id == dir->cache_id)
	/* The cached node is the same as DIR.  */
	{
	  register_pos_hit (c->stati);
	  pthread_spin_unlock (&cache_lock);
	  diskfs_nref (dir);
	  return dir;
	}
      else
	/* Just a normal entry in DIR; get the actual node.  */
	{
	  struct node *np;
	  error_t err;

	  register_pos_hit (c->stati);
	  pthread_spin_unlock (&cache_lock);

	  if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
	    {
	      pthread_mutex_unlock (&dir->lock);
	      err = diskfs_cached_lookup (id, &np);
	      pthread_mutex_lock (&dir->lock);

	      /* In the window where DP was unlocked, we might
		 have lost.  So check the cache again, and see
		 if it's still there; if so, then we win. */
	      c = find_cache (dir, "..", 2);
	      if (!c || c->node_cache_id != id)
		{
		  /* Lose */
		  pthread_mutex_unlock (&np->lock);
		  return 0;
		}
	    }
	  else
	    err = diskfs_cached_lookup (id, &np);
	  return err ? 0 : np;
	}
    }

  register_miss ();
  pthread_spin_unlock (&cache_lock);

  return 0;
}
