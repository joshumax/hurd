/* Integer-keyed hash table functions.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#ifndef __IHASH_H__
#define __IHASH_H__

#include <errno.h>


/* ---------------------------------------------------------------- */

typedef struct ihash *ihash_t;

struct ihash
{
  /* An array storing the elements in the hash table (each a void *).  */
  void **tab;

  /* An array storing the integer key for each element.  */
  int *ids;

  /* An array storing pointers to the `location pointers' for each element.
     These are used as cookies for quick 'n' easy removal.  */
  void ****locps;		/* four, count them, four stars */

  /* The length of all these arrays.  */
  int size;

  /* When freeing or overwriting an element, this function, if non-NULL, is
     called with the value as the first argument, and CLEANUP_ARG as the
     second argument.  */
  void (*cleanup)(void *element, void *arg);
  void *cleanup_arg;
};

/* Create an integer hash table and return it in HT.  If a memory allocation
   error occurs, ENOMEM is returned, otherwise 0.  */
error_t ihash_create(ihash_t *ht);

/* Free HT and all resources it consumes.  */
void ihash_free(ihash_t ht);

/* Sets HT's element cleanup function to CLEANUP, and its second argument to
   ARG.  CLEANUP will be called on the value of any element to be
   subsequently overwritten or deleted, with ARG as the second argument.  */
void ihash_set_cleanup(ihash_t ht,
		       void (*cleanup)(void *value, void *arg),
		       void *arg);

/* Add ITEM to the hash table HT under the key ID.  LOCP is the address of a
   pointer located in ITEM; If non-NULL, LOCP should point to a variable of
   type void **, and will be filled with a pointer that may be used as an
   argument to ihash_locp_remove() [the variable pointed to by LOCP may be
   written to subsequently between this call and when the element is
   deleted, so you can't stash its value elsewhere and hope to use the
   stashed value with ihash_locp_remove()].  If a memory allocation error
   occurs, ENOMEM is returned, otherwise 0.  */
error_t ihash_add(ihash_t ht, int id, void *item, void ***locp);

/* Find and return the item in hash table HT with key ID, or NULL if it
   doesn't exist.  */
void *ihash_find(ihash_t ht, int id);

/* Call function FUN of one arg for each element of HT.  FUN's only arg is a
   pointer to the value stored in the hash table.  If FUN ever returns
   non-zero, then iteration stops and ihash_iterate returns that value,
   otherwise it (eventually) returns 0.  */
error_t ihash_iterate(ihash_t ht, error_t (*fun)(void *));

/* Remove the entry with a key of ID from HT.  If anything was actually
   removed, 1 is returned, otherwise (if there was no such element), 0.  */
int ihash_remove(ihash_t ht, int id);

/* Remove the entry at LOCP from the hashtable HT.  LOCP is as returned from
   an earlier call to ihash_add().  This call should be faster than
   ihash_remove().  HT can be NULL, in which case the call still succeeds,
   but no cleanup can be done.  */
void ihash_locp_remove(ihash_t ht, void **ht_locp);

#endif /* __IHASH_H__ */
