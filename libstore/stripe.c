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

#include "store.h"

struct stripe_info
{
  struct store **stripes;
  int dealloc : 1;
};

static error_t
stripe_read (struct store *store,
	     off_t addr, size_t index, mach_msg_type_number_t amount,
	     char **buf, mach_msg_type_number_t *len)
{
  struct stripe_info *info = store->hook;
  return store_read (info->stripes[index], addr, amount, buf, len);
}

static error_t
stripe_write (struct store *store,
	      off_t addr, size_t index, char *buf, mach_msg_type_number_t len,
	      mach_msg_type_number_t *amount)
{
  struct store **stripes = store->hook;
  return store_write (stripes[index], addr, buf, len, amount);
}

static struct store_meths
stripe_meths = {stripe_read, stripe_write};

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE blocks (every store in STRIPES must
   have the same block size).  If DEALLOC is true, then the striped stores
   are freed when this store is (in any case, the array STRIPES is copied,
   and so should be freed by the caller).  */
error_t
store_ileave_create (struct store **stripes, size_t num_stripes, int dealloc,
		     off_t interleave, struct store **store)
{
  size_t i;
  error_t err = EINVAL;		/* default error */
  off_t block_size = 0, min_end = 0;
  off_t runs[num_stripes * 2];
  struct stripe_info *info = malloc (sizeof (struct stripe_info));

  if (info == 0)
    return ENOMEM;

  info->stripes = malloc (sizeof (struct store *) * num_stripes);
  info->dealloc = dealloc;

  if (info->stripes == 0)
    {
      free (info);
      return ENOMEM;
    }

  if (interleave == 0)
    goto barf;

  for (i = 0; i < num_stripes; i++)
    {
      runs[i * 2] = 0;
      runs[i * 2 + 1] = interleave;
      if (block_size == 0)
	{
	  block_size = stripes[i]->block_size;
	  min_end = stripes[i]->end;
	}
      else
	{
	  if (block_size != stripes[i]->block_size)
	    /* Mismatched block sizes; barf.  */
	    goto barf;
	  if (min_end > stripes[i]->end)
	    /* Only use as much space as the smallest stripe has.  */
	    min_end = stripes[i]->end;
	}
    }

  *store = _make_store (0, &stripe_meths, MACH_PORT_NULL, block_size,
			runs, num_stripes * 2, min_end);
  if (! *store)
    {
      err = ENOMEM;
      goto barf;
    }

  (*store)->hook = info;
  bcopy (stripes, info->stripes, num_stripes * sizeof *stripes);

  return 0;

 barf:
  free (info->stripes);
  free (info);
  return err;
}

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them) every store in STRIPES must have the same block size.
   If DEALLOC is true, then the sub-stores are freed when this store is (in
   any case, the array STORES is copied, and so should be freed by the
   caller).  */
error_t
store_concat_create (struct store **stores, size_t num_stores, int dealloc,
		     struct store **store)
{
  size_t i;
  error_t err = EINVAL;		/* default error */
  off_t block_size = 0;
  off_t runs[num_stores * 2];
  struct stripe_info *info = malloc (sizeof (struct stripe_info));

  if (info == 0)
    return ENOMEM;

  info->stripes = malloc (sizeof (struct store *) * num_stores);
  info->dealloc = dealloc;

  if (info->stripes == 0)
    {
      free (info);
      return ENOMEM;
    }

  for (i = 0; i < num_stores; i++)
    {
      runs[i * 2] = 0;
      runs[i * 2 + 1] = stores[i]->end;
      if (block_size == 0)
	block_size = stores[i]->block_size;
      else if (block_size != stores[i]->block_size)
	/* Mismatched block sizes; barf.  */
	goto barf;
    }

  *store = _make_store (0, &stripe_meths, MACH_PORT_NULL, block_size,
			runs, num_stores * 2, 0);
  if (! *store)
    {
      err = ENOMEM;
      goto barf;
    }

  (*store)->hook = info;
  bcopy (stores, info->stripes, num_stores * sizeof *stores);

  return 0;

 barf:
  free (info->stripes);
  free (info);
  return err;
}
