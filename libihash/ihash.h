/* ihash.h - Integer keyed hash table interface.
   Copyright (C) 1995, 2003, 2004, 2014, 2015 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>.
   Revised by Marcus Brinkmann <marcus@gnu.org>.

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
   along with the GNU Hurd; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef _HURD_IHASH_H
#define _HURD_IHASH_H	1

#include <errno.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>


/* The type of the values corresponding to the keys.  Must be a
   pointer type.  The values (hurd_ihash_value_t) 0 and
   (hurd_ihash_value_t) ~0 are reserved for the implementation.  */
typedef void *hurd_ihash_value_t;

/* When an value entry in the hash table is _HURD_IHASH_EMPTY or
   _HURD_IHASH_DELETED, then the location is available, and none of
   the other members of the item are valid at that index.  The
   difference is that searches continue though _HURD_IHASH_DELETED,
   but stop at _HURD_IHASH_EMPTY.  */
#define _HURD_IHASH_EMPTY	((hurd_ihash_value_t) 0)
#define _HURD_IHASH_DELETED	((hurd_ihash_value_t) -1)

/* Test if VALUE is valid.  */
static inline int
hurd_ihash_value_valid (hurd_ihash_value_t value)
{
  return value != _HURD_IHASH_EMPTY && value != _HURD_IHASH_DELETED;
}

/* The type of integer we want to use for the keys.  */
typedef uintptr_t hurd_ihash_key_t;

/* The type of a location pointer, which is a pointer to the hash
   value stored in the hash table.  */
typedef hurd_ihash_value_t *hurd_ihash_locp_t;


/* We support non-integer keys using the generalized key interface.

   To use it, supply a pair of functions matching the following
   specification, and use pointers to the key instead of the key
   itself in all calls to libihash.  */

/* The type of a function computing a hash for the given key.  */
typedef hurd_ihash_key_t (*hurd_ihash_fct_hash_t) (const void *);

/* The type of a function comparing two given keys.  Return true if
   both keys are equal.  */
typedef int (*hurd_ihash_fct_cmp_t) (const void *, const void *);


/* The type of the cleanup function, which is called for every value
   removed from the hash table.  */
typedef void (*hurd_ihash_cleanup_t) (hurd_ihash_value_t value, void *arg);


struct _hurd_ihash_item
{
  /* The value of this hash item.  Must be the first element of
     the struct for the HURD_IHASH_ITERATE macro.  */
  hurd_ihash_value_t value;

  /* The integer key of this hash item.  */
  hurd_ihash_key_t key;
};
typedef struct _hurd_ihash_item *_hurd_ihash_item_t;

struct hurd_ihash
{
  /* The number of hashed elements.  */
  size_t nr_items;

  /* An array of (key, value) pairs.  */
  _hurd_ihash_item_t items;

  /* The length of the array ITEMS.  */
  size_t size;

  /* The offset of the location pointer from the hash value.  */
  intptr_t locp_offset;

  /* The maximum load factor in binary percent.  */
  unsigned int max_load;

  /* When freeing or overwriting an element, this function is called
     with the value as the first argument, and CLEANUP_DATA as the
     second argument.  This does not happen if CLEANUP is NULL.  */
  hurd_ihash_cleanup_t cleanup;
  void *cleanup_data;

  /* User-supplied functions for the generalized key interface.  */
  hurd_ihash_fct_hash_t fct_hash;
  hurd_ihash_fct_cmp_t fct_cmp;

  /* Number of free slots.  */
  size_t nr_free;
};
typedef struct hurd_ihash *hurd_ihash_t;


/* Construction and destruction of hash tables.  */

/* The size of the initial allocation in number of items.  This must
   be a power of two.  */
#define HURD_IHASH_MIN_SIZE	32

/* The default value for the maximum load factor in binary percent.
   96b% is equivalent to 75%, 128b% to 100%.  */
#define HURD_IHASH_MAX_LOAD_DEFAULT 96

/* The LOCP_OFFS to use if no location pointer is available.  */
#define HURD_IHASH_NO_LOCP	INTPTR_MIN

/* The static initializer for a struct hurd_ihash.  */
#define HURD_IHASH_INITIALIZER(locp_offs)				\
  { .nr_items = 0, .size = 0, .cleanup = (hurd_ihash_cleanup_t) 0,	\
    .max_load = HURD_IHASH_MAX_LOAD_DEFAULT,				\
    .locp_offset = (locp_offs)}

#define HURD_IHASH_INITIALIZER_GKI(locp_offs, f_clean, f_clean_data,	\
				   f_hash, f_compare)			\
  { .nr_items = 0, .size = 0,						\
    .cleanup = (f_clean),						\
    .cleanup_data = (f_clean_data),					\
    .max_load = HURD_IHASH_MAX_LOAD_DEFAULT,				\
    .locp_offset = (locp_offs),						\
    .fct_hash = (f_hash),						\
    .fct_cmp = (f_compare)}						\

/* Initialize the hash table at address HT.  If LOCP_OFFSET is not
   HURD_IHASH_NO_LOCP, then this is an offset (in bytes) from the
   address of a hash value where a location pointer can be found.  The
   location pointer must be of type hurd_ihash_locp_t and can be used
   for fast removal with hurd_ihash_locp_remove().  */
void hurd_ihash_init (hurd_ihash_t ht, intptr_t locp_offs);

/* Destroy the hash table at address HT.  This first removes all
   elements which are still in the hash table, and calling the cleanup
   function for them (if any).  */
void hurd_ihash_destroy (hurd_ihash_t ht);

/* Create a hash table, initialize it and return it in HT.  If
   LOCP_OFFSET is not HURD_IHASH_NO_LOCP, then this is an offset (in
   bytes) from the address of a hash value where a location pointer
   can be found.  The location pointer must be of type
   hurd_ihash_locp_t and can be used for fast removal with
   hurd_ihash_locp_remove().  If a memory allocation error occurs,
   ENOMEM is returned, otherwise 0.  */
error_t hurd_ihash_create (hurd_ihash_t *ht, intptr_t locp_offs);

/* Destroy the hash table HT and release the memory allocated for it
   by hurd_ihash_create().  */
void hurd_ihash_free (hurd_ihash_t ht);


/* Configuration of the hash table.  */

/* Set the cleanup function for the hash table HT to CLEANUP.  The
   second argument to CLEANUP will be CLEANUP_DATA on every
   invocation.  */
void hurd_ihash_set_cleanup (hurd_ihash_t ht, hurd_ihash_cleanup_t cleanup,
			     void *cleanup_data);

/* Use the generalized key interface.  Must be called before any item
   is inserted into the table.	*/
void hurd_ihash_set_gki (hurd_ihash_t ht,
			 hurd_ihash_fct_hash_t fct_hash,
			 hurd_ihash_fct_cmp_t fct_cmp);

/* Set the maximum load factor in binary percent to MAX_LOAD, which
   should be between 64 and 128.  The default is
   HURD_IHASH_MAX_LOAD_DEFAULT.  New elements are only added to the
   hash table while the number of hashed elements is that much binary
   percent of the total size of the hash table.  If more elements are
   added, the hash table is first expanded and reorganized.  A
   MAX_LOAD of 128 will always fill the whole table before enlarging
   it, but note that this will increase the cost of operations
   significantly when the table is almost full.

   If the value is set to a smaller value than the current load
   factor, the next reorganization will happen when a new item is
   added to the hash table.  */
void hurd_ihash_set_max_load (hurd_ihash_t ht, unsigned int max_load);


/* Get the current load factor of HT in binary percent, where 128b%
   corresponds to 100%.  The reason we do this is that it is so
   efficient to compute:

   As the size is always a power of two, and 128 is also, the quotient
   of both is also a power of two.  Therefore, we can use bit shifts
   to scale the number of items.

   load = nr_items * 128 / size
        = nr_items * 2^{log2 (128) - log2 (size)}
        = nr_items >> (log2 (size) - log2 (128))
                                -- if size >= 128
        = nr_items << (log2 (128) - log2 (size))
                                -- otherwise

   If you want to convert this to percent, just divide by 1.28.  */
static inline unsigned int
hurd_ihash_get_load (hurd_ihash_t ht)
{
  int d = __builtin_ctzl (ht->size) - 7;
  return d >= 0 ? ht->nr_items >> d : ht->nr_items << -d;
}

/* Similar, but counts tombstones as well.  */
static inline unsigned int
hurd_ihash_get_effective_load (hurd_ihash_t ht)
{
  int d = __builtin_ctzl (ht->size) - 7;
  return
    d >= 0 ? (ht->size - ht->nr_free) >> d : (ht->size - ht->nr_free) << -d;
}

/* Add ITEM to the hash table HT under the key KEY.  If there already
   is an item under this key, call the cleanup function (if any) for
   it before overriding the value.  If a memory allocation error
   occurs, ENOMEM is returned, otherwise 0.  */
error_t hurd_ihash_add (hurd_ihash_t ht, hurd_ihash_key_t key,
			hurd_ihash_value_t item);

/* Add VALUE to the hash table HT under the key KEY at LOCP.  If there
   already is an item under this key, call the cleanup function (if
   any) for it before overriding the value.  This function is faster
   than hurd_ihash_add.

   If LOCP is NULL, fall back to hurd_ihash_add.  Otherwise, LOCP must
   be valid and may either be obtained from hurd_ihash_locp_find, or
   from an item that is currently in the hash table.  If an item is
   replaced, KEY must match the key of the previous item.

   If a memory allocation error occurs, ENOMEM is returned, otherwise
   0.  */
error_t hurd_ihash_locp_add (hurd_ihash_t ht, hurd_ihash_locp_t locp,
                             hurd_ihash_key_t key, hurd_ihash_value_t value);

/* Find and return the item in the hash table HT with key KEY, or NULL
   if it doesn't exist.  */
hurd_ihash_value_t hurd_ihash_find (hurd_ihash_t ht, hurd_ihash_key_t key);

/* Find and return the item in the hash table HT with key KEY, or NULL
   if it doesn't exist.  If it is not found, this function may still
   return a location in SLOT.

   If the lookup is successful, the returned location can be used with
   hurd_ihash_locp_add to update the item, and with
   hurd_ihash_locp_remove to remove it.

   If the lookup is not successful, the returned location can be used
   with hurd_ihash_locp_add to add the item.

   Note that returned location is only valid until the next insertion
   or deletion.  */
hurd_ihash_value_t hurd_ihash_locp_find (hurd_ihash_t ht,
					 hurd_ihash_key_t key,
					 hurd_ihash_locp_t *slot);

/* Iterate over all elements in the hash table.  You use this macro
   with a block, for example like this:

     error_t err;
     HURD_IHASH_ITERATE (ht, value)
       {
         err = foo (value);
         if (err)
           break;
       }
     if (err)
       cleanup_and_return ();

   Or even like this:

     HURD_IHASH_ITERATE (ht, value)
       foo (value);

   The block will be run for every element in the hash table HT.  The
   value of the current element is available in the variable VALUE
   (which is declared for you and local to the block).  */

/* The implementation of this macro is peculiar.  We want the macro to
   execute a block following its invocation, so we can only prepend
   code.  This excludes creating an outer block.  However, we must
   define two variables: The hash value variable VALUE, and the loop
   variable.

   We can define variables inside the for-loop initializer (C99), but
   we can only use one basic type to do that.  We can not use two
   for-loops, because we want a break statement inside the iterator
   block to terminate the operation.  So we must have both variables
   of the same basic type, but we can make one (or both) of them a
   pointer type.

   The pointer to the value can be used as the loop variable.  This is
   also the first element of the hash item, so we can cast the pointer
   freely between these two types.  The pointer is only dereferenced
   after the loop condition is checked (but of course the value the
   pointer pointed to must not have an influence on the condition
   result, so the comma operator is used to make sure this
   subexpression is always true).  */
#define HURD_IHASH_ITERATE(ht, val)					\
  for (hurd_ihash_value_t val,						\
         *_hurd_ihash_valuep = (ht)->size ? &(ht)->items[0].value : 0;	\
       (ht)->size							\
	 && (size_t) ((_hurd_ihash_item_t) _hurd_ihash_valuep		\
		      - &(ht)->items[0])				\
            < (ht)->size						\
         && (val = *_hurd_ihash_valuep, 1);				\
       _hurd_ihash_valuep = (hurd_ihash_value_t *)			\
	 (((_hurd_ihash_item_t) _hurd_ihash_valuep) + 1))		\
    if (val != _HURD_IHASH_EMPTY && val != _HURD_IHASH_DELETED)

/* Iterate over all elements in the hash table making both the key and
   the value available.  You use this macro with a block, for example
   like this:

     HURD_IHASH_ITERATE_ITEMS (ht, item)
       foo (item->key, item->value);

   The block will be run for every element in the hash table HT.  The
   key and value of the current element is available as ITEM->key and
   ITEM->value.  */
#define HURD_IHASH_ITERATE_ITEMS(ht, item)                              \
  for (_hurd_ihash_item_t item = (ht)->size? &(ht)->items[0]: 0;	\
       (ht)->size && item - &(ht)->items[0] < (ht)->size;               \
       item++)                                                          \
    if (item->value != _HURD_IHASH_EMPTY &&                             \
        item->value != _HURD_IHASH_DELETED)

/* Remove the entry with the key KEY from the hash table HT.  If such
   an entry was found and removed, 1 is returned, otherwise 0.  */
int hurd_ihash_remove (hurd_ihash_t ht, hurd_ihash_key_t key);

/* Remove the entry pointed to by the location pointer LOCP from the
   hash table HT.  LOCP is the location pointer of which the address
   was provided to hurd_ihash_add().  This call is faster than
   hurd_ihash_remove().  */
void hurd_ihash_locp_remove (hurd_ihash_t ht, hurd_ihash_locp_t locp);

/* We provide a general purpose hash function.  This function can be
   used with the generalized key interface to use arbitrary data as
   keys using this library.  */
uint32_t hurd_ihash_hash32 (const void *buf, size_t len, uint32_t seed);

#endif	/* _HURD_IHASH_H */
