/* Striped store backend

   Copyright (C) 1996,97,99,2001, 2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdlib.h>
#include <string.h>

#include "store.h"

extern long lcm (long p, long q);

/* Return ADDR adjust for any block size difference between STORE and
   STRIPE.  We assume that STORE's block size is no less than STRIPE's.  */
static inline store_offset_t
addr_adj (store_offset_t addr, struct store *store, struct store *stripe)
{
  unsigned common_bs = store->log2_block_size;
  unsigned stripe_bs = stripe->log2_block_size;
  if (common_bs == stripe_bs)
    return addr;
  else
    return addr << (common_bs - stripe_bs);
}

static error_t
stripe_read (struct store *store,
	     store_offset_t addr, size_t index, size_t amount,
	     void **buf, size_t *len)
{
  struct store *stripe = store->children[index];
  return store_read (stripe, addr_adj (addr, store, stripe), amount, buf, len);
}

static error_t
stripe_write (struct store *store,
	      store_offset_t addr, size_t index,
	      const void *buf, size_t len, size_t *amount)
{
  struct store *stripe = store->children[index];
  return
    store_write (stripe, addr_adj (addr, store, stripe), buf, len, amount);
}

error_t
stripe_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
}

error_t
stripe_remap (struct store *source,
	      const struct store_run *runs, size_t num_runs,
	      struct store **store)
{
  return store_remap_create (source, runs, num_runs, 0, store);
}

error_t
ileave_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  enc->num_ints += 4;
  return store_allocate_child_encodings (store, enc);
}

error_t
ileave_encode (const struct store *store, struct store_enc *enc)
{
  enc->ints[enc->cur_int++] = store->class->id;
  enc->ints[enc->cur_int++] = store->flags;
  enc->ints[enc->cur_int++] = store->wrap_dst; /* interleave factor */
  enc->ints[enc->cur_int++] = store->num_children;
  return store_encode_children (store, enc);
}

error_t
ileave_decode (struct store_enc *enc, const struct store_class *const *classes,
	       struct store **store)
{
  if (enc->cur_int + 4 > enc->num_ints)
    return EINVAL;
  else
    {
      int type __attribute__((unused)) = enc->ints[enc->cur_int++];
      int flags = enc->ints[enc->cur_int++];
      int interleave = enc->ints[enc->cur_int++];
      int nkids = enc->ints[enc->cur_int++];
      struct store *kids[nkids];
      error_t err = store_decode_children (enc, nkids, classes, kids);
      if (! err)
	err =  store_ileave_create (kids, nkids, interleave, flags, store);
      return err;
    }
}

const struct store_class
store_ileave_class =
{
  STORAGE_INTERLEAVE, "interleave", stripe_read, stripe_write, stripe_set_size,
  ileave_allocate_encoding, ileave_encode, ileave_decode,
  store_set_child_flags, store_clear_child_flags, 0, 0, stripe_remap
};
STORE_STD_CLASS (ileave);

error_t
concat_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  enc->num_ints += 3;
  return store_allocate_child_encodings (store, enc);
}

error_t
concat_encode (const struct store *store, struct store_enc *enc)
{
  enc->ints[enc->cur_int++] = store->class->id;
  enc->ints[enc->cur_int++] = store->flags;
  enc->ints[enc->cur_int++] = store->num_children;
  return store_encode_children (store, enc);
}

error_t
concat_decode (struct store_enc *enc, const struct store_class *const *classes,
	       struct store **store)
{
  if (enc->cur_int + 3 > enc->num_ints)
    return EINVAL;
  else
    {
      int type __attribute__((unused)) = enc->ints[enc->cur_int++];
      int flags = enc->ints[enc->cur_int++];
      int nkids = enc->ints[enc->cur_int++];
      struct store *kids[nkids];
      error_t err = store_decode_children (enc, nkids, classes, kids);
      if (! err)
	err =  store_concat_create (kids, nkids, flags, store);
      return err;
    }
}

const struct store_class
store_concat_class =
{
  STORAGE_CONCAT, "concat", stripe_read, stripe_write, stripe_set_size,
  concat_allocate_encoding, concat_encode, concat_decode,
  store_set_child_flags, store_clear_child_flags, 0, 0, stripe_remap,
  store_concat_open
};
STORE_STD_CLASS (concat);

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE bytes; INTERLEAVE must be an
   integer multiple of each stripe's block size.  The stores in STRIPES are
   consumed -- that is, will be freed when this store is (however, the
   *array* STRIPES is copied, and so should be freed by the caller).  */
error_t
store_ileave_create (struct store *const *stripes, size_t num_stripes,
		     store_offset_t interleave, int flags,
		     struct store **store)
{
  size_t i;
  error_t err;
  size_t block_size = 1;
  store_offset_t min_end = 0;
  struct store_run runs[num_stripes];
  int common_flags = STORE_BACKEND_FLAGS;

  /* Find a common block size.  */
  for (i = 0; i < num_stripes; i++)
    block_size = lcm (block_size, stripes[i]->block_size);

  if (interleave < block_size || (interleave % block_size) != 0)
    return EINVAL;

  interleave /= block_size;

  for (i = 0; i < num_stripes; i++)
    {
       /* The stripe's end adjusted to the common block size.  */
      store_offset_t end = stripes[i]->end;

      runs[i].start = 0;
      runs[i].length = interleave;

      if (stripes[i]->block_size != block_size)
	end /= (block_size / stripes[i]->block_size);

      if (min_end < 0)
	min_end = end;
      else if (min_end > end)
	/* Only use as much space as the smallest stripe has.  */
	min_end = end;

      common_flags &= stripes[i]->flags;
    }

  err = _store_create (&store_ileave_class, MACH_PORT_NULL,
		       common_flags | flags, block_size,
		       runs, num_stripes, min_end, store);
  if (! err)
    {
      (*store)->wrap_dst = interleave;

      err = store_set_children (*store, stripes, num_stripes);
      if (err)
	store_free (*store);
    }

  return err;
}

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them).  The stores in STRIPES are consumed -- that is, will
   be freed when this store is (however, the *array* STRIPES is copied, and
   so should be freed by the caller).  */
error_t
store_concat_create (struct store * const *stores, size_t num_stores,
		     int flags, struct store **store)
{
  size_t i;
  error_t err;
  size_t block_size = 1;
  int common_flags = STORE_BACKEND_FLAGS;
  struct store_run runs[num_stores];

  /* Find a common block size.  */
  for (i = 0; i < num_stores; i++)
    block_size = lcm (block_size, stores[i]->block_size);

  for (i = 0; i < num_stores; i++)
    {
      runs[i].start = 0;
      runs[i].length = stores[i]->end;
      common_flags &= stores[i]->flags;
    }

  err = _store_create (&store_concat_class, MACH_PORT_NULL,
		       flags | common_flags, block_size,
		       runs, num_stores, 0, store);
  if (! err)
    {
      err = store_set_children (*store, stores, num_stores);
      if (! err)
	{
	  err = store_children_name (*store, &(*store)->name);
	  if (err == EINVAL)
	    err = 0;		/* Can't find a name; deal. */
	}
      if (err)
	store_free (*store);
    }

  return err;
}

/* Return a new store that concatenates the stores created by opening all the
   individual stores described in NAME; for the syntax of NAME, see
   store_open_children.  */
error_t
store_concat_open (const char *name, int flags,
		   const struct store_class *const *classes,
		   struct store **store)
{
  struct store **stores;
  size_t num_stores;
  error_t err =
    store_open_children (name, flags, classes, &stores, &num_stores);
  if (! err)
    {
      err = store_concat_create (stores, num_stores, flags, store);
      if (err)
	{
	  size_t k;
	  for (k = 0; k < (*store)->num_children; k++)
	    store_free ((*store)->children[k]);
	}
    }
  return err;
}
