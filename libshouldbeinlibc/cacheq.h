/* Helper functions for maintaining a fixed-size lru-ordered queue

   Copyright (C) 1996 Free Software Foundation, Inc.

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

#ifndef __CACHEQ_H__
#define __CACHEQ_H__

#include <stddef.h>
#include <errno.h>

/* This header occurs at the start of every cacheq entry.  */
struct cacheq_hdr
{
  /* Next and prev entries in the cache, linked in LRU order.  These are of
     type `void *' so that it's conveient to iterate through the list using a
     variable pointing to a structure that contains the header, by using
     something like `VAR = VAR->hdr.next'.  */
  void *next, *prev;
};

/* A cacheq.  Note that this structure is laid out to allow convenient use as
   static initialized data.  */
struct cacheq
{
  /* The size of each entry, including its cacheq_hdr.  */
  size_t entry_size;

  /* If non-0, then when making new entries (for instance, when the cacheq is
     initialized, or when its size is increased), this function is called on
     each new entry (with it's header already initialized).  If this function
     isn't defined, then each entry is simply zeroed.  */
  void (*init_entry) (void *entry);

  /* When an entry is moved from one place in memory to another (for
     instance, changing the size of the cache, new storage is used), this is
     called for each entry, with FROM and TO the old and new locations of the
     entry (and TO contains a bitwise copy of FROM).  This is often useful
     when the entry points to something that contains a backpointer to it.  */
  void (*move_entry) (void *from, void *to);

  /* When entries are removed for some reason (for instance, when reducing
     the size of the cacheq), this function is called on each.  */
  void (*finalize_entry) (void *entry);

  /* The number of entries in the cache.  This number is fixed.  */
  int length;

  /* A buffer holding malloc'd memory for all the entries -- NUM_ENTRIES
     entries of size ENTRY_SIZE.  */
  void *entries;

  /* The least, and most, recently used entries in the cache.  These point to
     either end of a linked list composed of all the elements of the cache.
     This list will always be the same length -- if an element is `removed',
     its entry is simply marked inactive, and moved to the LRU end of the list
     so it will be reused first.  These pointers are of type `void *' so they
     can be conveniently used by client code (see comment in struct
     cacheq_hdr). */
  void *lru, *mru;
};

/* Move ENTRY to the most-recently-used end of CACHEQ.  */
void cacheq_make_mru (struct cacheq *cq, void *entry);

/* Move ENTRY to the least-recently-used end of CACHEQ. */
void cacheq_make_lru (struct cacheq *cq, void *entry);

/* Change CQ's size to be LENGTH entries.  */
error_t cacheq_set_length (struct cacheq *cq, int length);

#endif /* __CACHEQ_H__ */
