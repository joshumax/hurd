/* Striped store backend

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <string.h>

#include "store.h"

extern long lcm (long p, long q);

/* Return ADDR adjust for any block size difference between STORE and
   STRIPE.  We assume that STORE's block size is no less than STRIPE's.  */
static inline off_t
addr_adj (off_t addr, struct store *store, struct store *stripe)
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
	     off_t addr, size_t index, mach_msg_type_number_t amount,
	     char **buf, mach_msg_type_number_t *len)
{
  struct store *stripe = store->children[index];
  return store_read (stripe, addr_adj (addr, store, stripe), amount, buf, len);
}

static error_t
stripe_write (struct store *store,
	      off_t addr, size_t index, char *buf, mach_msg_type_number_t len,
	      mach_msg_type_number_t *amount)
{
  struct store *stripe = store->children[index];
  return
    store_write (stripe, addr_adj (addr, store, stripe), buf, len, amount);
}

static struct store_meths
stripe_meths = {stripe_read, stripe_write, 0, 0, 0, 0};

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE bytes; INTERLEAVE must be an
   integer multiple of each stripe's block size.  The stores in STRIPES are
   consumed -- that is, will be freed when this store is (however, the
   *array* STRIPES is copied, and so should be freed by the caller).  */
error_t
store_ileave_create (struct store *const *stripes, size_t num_stripes,
		     off_t interleave, struct store **store)
{
  size_t i;
  error_t err;
  off_t block_size = 1, min_end = 0;
  struct store_run runs[num_stripes];

  /* Find a common block size.  */
  for (i = 0; i < num_stripes; i++)
    block_size = lcm (block_size, stripes[i]->block_size);

  if (interleave < block_size || (interleave % block_size) != 0)
    return EINVAL;

  interleave /= block_size;

  for (i = 0; i < num_stripes; i++)
    {
       /* The stripe's end adjusted to the common block size.  */
      off_t end = stripes[i]->end;

      runs[i].start = 0;
      runs[i].length = interleave;

      if (stripes[i]->block_size != block_size)
	end /= (block_size / stripes[i]->block_size);
  
      if (min_end < 0)
	min_end = end;
      else if (min_end > end)
	/* Only use as much space as the smallest stripe has.  */
	min_end = end;
    }

  *store = _make_store (0, &stripe_meths, MACH_PORT_NULL, block_size,
			runs, num_stripes, min_end);
  if (! *store)
    return ENOMEM;

  (*store)->wrap_dst = interleave;

  err = store_set_children (*store, stripes, num_stripes);
  if (err)
    store_free (*store);

  return err;
}

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them).  The stores in STRIPES are consumed -- that is, will
   be freed when this store is (however, the *array* STRIPES is copied, and
   so should be freed by the caller).  */
error_t
store_concat_create (struct store * const *stores, size_t num_stores,
		     struct store **store)
{
  size_t i;
  error_t err;
  off_t block_size = 1;
  struct store_run runs[num_stores];

  /* Find a common block size.  */
  for (i = 0; i < num_stores; i++)
    block_size = lcm (block_size, stores[i]->block_size);

  for (i = 0; i < num_stores; i++)
    {
      runs[i].start = 0;
      runs[i].length = stores[i]->end;
    }

  *store = _make_store (0, &stripe_meths, MACH_PORT_NULL, block_size,
			runs, num_stores * 2, 0);
  if (! *store)
    return ENOMEM;

  err = store_set_children (*store, stores, num_stores);
  if (err)
    store_free (*store);

  return err;
}
