/* Integer-keyed hash table functions.

   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.
   
   This file is part of the GNU Hurd.

   Written by Michael I. Bushnell; revised by Miles Bader <miles@gnu>.
   
   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   The GNU Hurd is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <string.h>
#include <stdlib.h>

#include "ihash.h"

/* ---------------------------------------------------------------- */

/* When an entry in a hashtable's TAB array is HASH_EMPTY, that location is
   available, and none of the other arrays are valid at that index.  */
#define HASH_EMPTY 0

/* When an entry in a hashtable's TAB array is HASH_DEL, that location is
   available, and none of the other arrays are valid at that index.  The
   difference from HASH_EMPTY is that searches continue though HASH_DEL and
   stop at HASH_EMPTY.  */
#define HASH_DEL ((void *) -1)

/* Returns an initial index in HT for the key ID, for search for an entry.  */
#define HASH(ht, id) ((id) % (ht)->size)
/* Returns subsequent indices in HT for the key ID, given the previous one.  */
#define REHASH(ht, id, h) (((id) + (h)) % (ht)->size)

/* ---------------------------------------------------------------- */

static inline int
index_empty(ihash_t ht, int index)
{
  return ht->tab[index] == HASH_EMPTY || ht->tab[index] == HASH_DEL;
}

static inline int
index_valid(ihash_t ht, int index, int id)
{
  return !index_empty(ht, index) && ht->ids[index] == id;
}

/* Given a hash table HT, and a key ID, finds the index in the table of that
   key.  You must subsequently check to see whether the given index is valid
   (with index_valid() or index_empty()).  */
static inline int
find_index(ihash_t ht, int id)
{
  int h, firsth = -1;

  for (h = HASH(ht, id); 
       ht->tab[h] != HASH_EMPTY && ht->ids[h] != id && h != firsth;
       h = REHASH(ht, id, h))
    if (firsth == -1)
      firsth = h;

  return h;
}

/* ---------------------------------------------------------------- */

/* Create an integer hash table and return it in HT.  If a memory allocation
   error occurs, ENOMEM is returned, otherwise 0.  */
error_t
ihash_create(ihash_t *ht)
{
  *ht = malloc(sizeof(struct ihash));
  if (*ht == NULL)
    return ENOMEM;
  (*ht)->size = 0;
  return 0;
}

/* Free HT and all resources it consumes.  */
void 
ihash_free(ihash_t ht)
{
  void (*cleanup)(void *value, void *arg) = ht->cleanup;

  if (cleanup)
    {
      int i;
      void *arg = ht->cleanup_arg;
      for (i = 0; i < ht->size; i++)
	if (!index_empty(ht, i))
	  (*cleanup)(ht->tab[i], arg);
    }

  if (ht->size > 0)
    {
      free(ht->tab);
      free(ht->ids);
      free(ht->locps);
    }

  free(ht);
}

/* Sets HT's element cleanup function to CLEANUP, and its second argument to
   ARG.  CLEANUP will be called on the value of any element to be
   subsequently overwritten or deleted, with ARG as the second argument.  */
void
ihash_set_cleanup(ihash_t ht,
		  void (*cleanup)(void *value, void *arg),
		  void *arg)
{
  ht->cleanup = cleanup;
  ht->cleanup_arg = arg;
}

/* ---------------------------------------------------------------- */

/* Add ITEM to the hash table HT under the key ID.  LOCP is the address of a
   pointer located in ITEM; If non-NULL, LOCP should point to a variable of
   type void **, and will be filled with a pointer that may be used as an
   argument to ihash_locp_remove().  The variable pointed to by LOCP may be
   written to subsequently between this called and when the element is
   deleted, so you can't stash its value elsewhere and hope to use the
   stashed value with ihash_locp_remove().  If a memory allocation error
   occurs, ENOMEM is returned, otherwise 0.  */
error_t
ihash_add(ihash_t ht, int id, void *item, void ***locp)
{
  if (ht->size)
    {
      int h, firsth = -1;

      /* Search for for an empty or deleted space.  */
      for (h = HASH(ht, id); 
	   ht->tab[h] != HASH_EMPTY && ht->tab[h] != HASH_DEL && h != firsth;
	   h = REHASH(ht, id, h))
	if (firsth == -1)
	  firsth = h;

      if (index_empty(ht, h) || ht->ids[h] == id)
	{
	  if (!index_empty(ht, h) && ht->cleanup)
	    ht->cleanup(ht->tab[h], ht->cleanup_arg);

	  ht->tab[h] = item;
	  ht->ids[h] = id;
	  ht->locps[h] = locp;

	  if (locp)
	    *locp = &ht->tab[h];

	  return 0;
	}
    }

  {
    int i;
    void **entry;
    int old_size = ht->size;
    void **old_tab = ht->tab;
    void ****old_locps = ht->locps;
    int *old_ids = ht->ids;

    ht->size = _ihash_nextprime (2 * old_size);
    ht->tab = malloc(ht->size * sizeof (void *));
    ht->locps = malloc (ht->size * sizeof (void ***));
    ht->ids = malloc (ht->size * sizeof (int));
      
    if (ht->tab == NULL || ht->locps == NULL || ht->ids == NULL)
      /* Memory allocation error; back out our changes and fail...  */
      {
	if (ht->tab) free(ht->tab);
	if (ht->locps) free(ht->locps);
	if (ht->ids) free(ht->ids);

	ht->size = old_size;
	ht->tab = old_tab;
	ht->locps = old_locps;
	ht->ids = old_ids;

	return ENOMEM;
      }

    for (i = ht->size, entry = ht->tab; i > 0; i--, entry++)
      *entry = HASH_EMPTY;

    /* We have to rehash this again?  */
    if (old_size > 0)
      for (i = 0; i < old_size; i++)
	if (old_tab[i] != HASH_EMPTY && old_tab[i] != HASH_DEL)
	  ihash_add(ht, old_ids[i], old_tab[i], old_locps[i]);

    /* Finally add the new element!  */
    ihash_add(ht, id, item, locp);
  
    if (old_size > 0)
      {
	free(old_tab);
	free(old_locps);
	free(old_ids);
      }

    return 0;
  }
}

/* Find and return the item in hash table HT with key ID, or NULL if it
   doesn't exist.  */
void *
ihash_find (ihash_t ht, int id)
{
  if (ht->size == 0)
    return 0;
  else
    {
      int index = find_index(ht, id);
      return index_valid(ht, index, id) ? ht->tab[index] : 0;
    }
}

/* ---------------------------------------------------------------- */

/* Call function FUN of one arg for each element of HT.  FUN's only arg is a
   pointer to the value stored in the hash table.  If FUN ever returns
   non-zero, then iteration stops and ihash_iterate returns that value,
   otherwise it (eventually) returns 0.  */
error_t
ihash_iterate(ihash_t ht, error_t (*fun)(void *))
{
  int i;
  for (i = 0; i < ht->size; i++)
    if (!index_empty(ht, i))
      {
	error_t err = fun(ht->tab[i]);
	if (err)
	  return err;
      }
  return 0;
}

/* Remove the entry at LOCP from the hashtable HT.  LOCP is as returned from
   an earlier call to ihash_add().  This call should be faster than
   ihash_remove().  HT can be NULL, in which case the call still succeeds,
   but no cleanup can be done.  */
void
ihash_locp_remove(ihash_t ht, void **locp)
{
  if (ht && ht->cleanup)
    ht->cleanup(*locp, ht->cleanup_arg);
  *locp = HASH_DEL;
}

/* Remove the entry with a key of ID from HT.  If anything was actually
   removed, 1 is returned, otherwise (if there was no such element), 0.  */
int
ihash_remove(ihash_t ht, int id)
{
  int index = find_index(ht, id);
  if (index_valid(ht, index, id))
    {
      ihash_locp_remove(ht, &ht->tab[index]);
      return 1;
    }
  else
    return 0;
}
