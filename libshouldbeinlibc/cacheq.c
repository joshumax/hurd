/* Helper functions for maintaining a fixed-size lru-ordered queue

   Copyright (C) 1996, 1998 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <string.h>
#include <stdlib.h>

#include "cacheq.h"

/* Move ENTRY to the most-recently-used end of CACHEQ.  */
void
cacheq_make_mru (struct cacheq *cq, void *entry)
{
  struct cacheq_hdr *h = entry;

  if (h != cq->mru)
    {
      /* First remove it.  We known H->prev isn't 0 because H wasn't
	 previously == MRU.  */
      ((struct cacheq_hdr *)h->prev)->next = h->next;
      if (h->next)
	((struct cacheq_hdr *)h->next)->prev = h->prev;
      else
	cq->lru = h->prev;

      /* Now make it MRU.  */
      h->next = cq->mru;
      h->prev = 0;
      ((struct cacheq_hdr *)cq->mru)->prev = h;
      cq->mru = h;
    }
}

/* Move ENTRY to the least-recently-used end of CACHEQ. */
void
cacheq_make_lru (struct cacheq *cq, void *entry)
{
  struct cacheq_hdr *h = entry;

  if (h != cq->lru)
    {
      /* First remove it.  We known H->next isn't 0 because H wasn't
	 previously == LRU.  */
      ((struct cacheq_hdr *)h->next)->prev = h->prev;
      if (h->prev)
	((struct cacheq_hdr *)h->prev)->next = h->next;
      else
	cq->mru = h->next;

      /* Now make it LRU.  */
      h->prev = cq->lru;
      h->next = 0;
      ((struct cacheq_hdr *)cq->lru)->next = h;
      cq->lru = h;
    }
}

/* Change CQ's size to be LENGTH entries.  */
error_t
cacheq_set_length (struct cacheq *cq, int length)
{
  if (length != cq->length)
    {
      size_t esz = cq->entry_size;
      void *new_entries = malloc (esz * length);
      /* Source  entries.  */
      struct cacheq_hdr *fh = cq->mru;
      /* Destination entries (and limit).  */
      struct cacheq_hdr *th = new_entries;
      struct cacheq_hdr *end = new_entries + esz * (length - 1);
      struct cacheq_hdr *prev_th = 0;

      if (! new_entries)
	return ENOMEM;

      while (fh || th)
	{
	  struct cacheq_hdr *next_th =
	    (!th || th >= end) ? 0 : (void *)th + esz;

	  if (fh && th)
	    memcpy (th, fh, esz);	/* Copy the bits in a moved entry.  */
	  else if (th)
	    memset (th, 0, esz);	/* Zero the bits in a new entry.  */

	  if (th)
	    /* Fixup headers.  */
	    {
	      th->prev = prev_th;
	      th->next = next_th;
	    }

	  /* Call user hooks as appropriate.  */
	  if (fh && th)
	    {
	      if (cq->move_entry)
		(*cq->move_entry) (fh, th);
	    }
	  else if (th)
	    {
	      if (cq->init_entry)
		(*cq->init_entry) (th);
	    }
	  else
	    {
	      if (cq->finalize_entry)
		(*cq->finalize_entry) (fh);
	    }
	    
	  if (fh)
	    fh = fh->next;
	  if (th)
	    {
	      prev_th = th;
	      th = next_th;
	    }
	}

      free (cq->entries);
      cq->entries = new_entries;
      cq->mru = new_entries;
      cq->lru = prev_th;
      cq->length = length;
    }

  return 0;
}
