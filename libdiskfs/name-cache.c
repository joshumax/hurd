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

/* Current number of names cached */
static int cache_cur;

/* Cache entry */
struct lookup_cache
{
  struct lookup_cache *next, *prev;
  
  /* These do not hold references; special code exists to call
     purge routines when the nodes lose refs. */
  struct node *dp;		/* directory */
  struct node *np;		/* node */

  /* strlen of name member */
  size_t namelen;
  char name[0];
};

/* Cache itself */
static struct lookup_cache *lookup_cache_head, *lookup_cache_tail;

/* Buffer to hold statistics */
static struct
{
  long pos_hits;
  long neg_hits;
  long miss;
} statistics;

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void
diskfs_enter_cache (struct node *dir, struct node *np, char *name)
{
  struct lookup_cache *lc, *tmp = 0;
  size_t len = strlen (name) + 1;
  
  
  lc = malloc (sizeof (struct lookup_cache) + len);
  lc->dp = dir;
  lc->np = np;
  lc->namelen = len - 1;
  bcopy (name, lc->name, len);

  spin_lock (&diskfs_node_refcnt_lock);
  if (cache_cur == MAXCACHE)
    {
      /* Free the entry at the tail */
      tmp = lookup_cache_tail;
      lookup_cache_tail = lookup_cache_tail->prev;
      if (lookup_cache_tail)
	lookup_cache_tail->next = 0;
      else
	lookup_cache_head = 0;
    }
  else
    cache_cur++;
  
  /* Add the new entry at the front */
  lc->next = lookup_cache_head;
  lc->prev = 0;
  if (lc->next)
    lc->next->prev = lc;
  lookup_cache_head = lc;
  if (!lc->next)
    lookup_cache_tail = lc;
  
  spin_unlock (&diskfs_node_refcnt_lock);
  
  if (tmp)
    free (tmp);
}

/* Purge all references in the cache to NP as a node inside 
   directory DP. */
void
diskfs_purge_cache (struct node *dp, struct node *np)
{
  struct lookup_cache *lc, *nxt;
  
  spin_lock (&diskfs_node_refcnt_lock);
  for (lc = lookup_cache_head; lc; lc = nxt)
    if (lc->np == np && lc->dp == dp)
      {
	if (lc->prev)
	  lc->prev->next = lc->next;
	if (lc->next)
	  lc->next->prev = lc->prev;
	nxt = lc->next;
	if (lookup_cache_tail == lc)
	  lookup_cache_tail = lc->prev;
	free (lc);
      }
    else
      nxt = lc->next;
  spin_unlock (&diskfs_node_refcnt_lock);
}

/* Purge all references in the cache to NP, either as a node or as a
   directory.  diskfs_node_refcnt_lock must be held around this call. */
void 
_diskfs_purge_cache_deletion (struct node *np)
{
  struct lookup_cache *lc, *nxt;
  
  for (lc = lookup_cache_head; lc; lc = nxt)
    if (lc->np == np || lc->dp == np)
      {
	if (lc->prev)
	  lc->prev->next = lc->next;
	if (lc->next)
	  lc->next->prev = lc->prev;
	nxt = lc->next;
	if (lookup_cache_tail == lc)
	  lookup_cache_tail = lc->prev;
	free (lc);
      }
    else
      nxt = lc->next;
}

/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *
diskfs_check_cache (struct node *dir, char *name)
{
  struct lookup_cache *lc;
  size_t len = strlen (name);
  
  spin_lock (&diskfs_node_refcnt_lock);
  for (lc = lookup_cache_head; lc; lc = lc->next)
    if (lc->dp == dir
	&& lc->namelen == len
	&& lc->name[0] == name[0]
	&& !strcmp (lc->name, name))
      {
	if (lc->np)
	  {
	    lc->np->references++;
	    statistics.pos_hits++;
	    spin_unlock (&diskfs_node_refcnt_lock);
	    return lc->np;
	  }
	else
	  {
	    statistics.neg_hits++;
	    spin_unlock (&diskfs_node_refcnt_lock);
	    return (struct node *) -1;
	  }
      }
  statistics.miss++;
  spin_unlock (&diskfs_node_refcnt_lock);
  return 0;
}

