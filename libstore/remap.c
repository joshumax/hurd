/* Block address translation

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

#include <malloc.h>
#include <hurd/fs.h>

#include "store.h"

static error_t
remap_read (struct store *store,
	     off_t addr, size_t index, size_t amount,
	     void **buf, size_t *len)
{
  return store_read (store->children[0], addr, amount, buf, len);
}

static error_t
remap_write (struct store *store,
	      off_t addr, size_t index, void *buf, size_t len,
	      size_t *amount)
{
  return store_write (store->children[0], addr, buf, len, amount);
}

error_t
remap_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  enc->num_ints += 3;
  enc->num_offsets += store->num_runs * 2;
  return store_allocate_child_encodings (store, enc);
}

error_t
remap_encode (const struct store *store, struct store_enc *enc)
{
  int i;
  enc->ints[enc->cur_int++] = store->class->id;
  enc->ints[enc->cur_int++] = store->flags;
  enc->ints[enc->cur_int++] = store->num_runs;
  for (i = 0; i < store->num_runs; i++)
    {
      enc->offsets[enc->cur_offset++] = store->runs[i].start;
      enc->offsets[enc->cur_offset++] = store->runs[i].length;
    }
  return store_encode_children (store, enc);
}

error_t
remap_decode (struct store_enc *enc, const struct store_class *const *classes,
	      struct store **store)
{
  if (enc->cur_int + 3 > enc->num_ints)
    return EINVAL;
  else
    {
      int type = enc->ints[enc->cur_int++];
      int flags = enc->ints[enc->cur_int++];
      int num_runs = enc->ints[enc->cur_int++];
      error_t create_remap (const struct store_run *runs, size_t num_runs)
	{
	  struct store *source;
	  error_t err = store_decode_children (enc, 1, classes, &source);
	  if (! err)
	    err =  store_remap_create (source, runs, num_runs, flags, store);
	  return err;
	}
      return store_with_decoded_runs (enc, num_runs, create_remap);
    }
}

struct store_class
store_remap_class =
{
  STORAGE_REMAP, "remap", remap_read, remap_write,
  remap_allocate_encoding, remap_encode, remap_decode
};

/* Return a new store in STORE that reflects the blocks in RUNS & RUNS_LEN
   from SOURCE; SOURCE is consumed, but RUNS is not.  Unlike the
   store_remap function, this function always operates by creating a new
   store of type `remap' which has SOURCE as a child, and so may be less
   efficient than store_remap for some types of stores.  */
error_t
store_remap_create (struct store *source,
		    const struct store_run *runs, size_t num_runs,
		    int flags, struct store **store)
{
  error_t err;

  *store = _make_store (&store_remap_class, MACH_PORT_NULL, flags,
			source->block_size, runs, num_runs, 0);
  if (! *store)
    return ENOMEM;

  err = store_set_children (*store, &source, 1);
  if (err)
    store_free (*store);

  return err;
}

/* For each run in RUNS, of length NUM_RUNS, translate the  */
error_t
store_remap_runs (const struct store_run *runs, size_t num_runs,
		  const struct store_run *base_runs, size_t num_base_runs,
		  struct store_run **xruns, size_t *num_xruns)
{
  int i, j;
  size_t xruns_alloced = num_runs + num_base_runs;

  /* Add the single run [ADDR, LEN) to *XRUNS, returning true if successful. */
  int add_run (off_t addr, off_t len)
    {
      if (*num_xruns == xruns_alloced)
	/* Make some more space in *XRUNS.  */
	{
	  struct store_run *new;
	  xruns_alloced *= 2;
	  new = realloc (*xruns, xruns_alloced * sizeof (struct store_run));
	  if (! new)
	    return 0;
	  *xruns = new;
	}
      (*xruns)[(*num_xruns)++] = (struct store_run){ addr, len };
      return 1;
    }

  *xruns = malloc (xruns_alloced * sizeof (struct store_run));
  if (! *xruns)
    return ENOMEM;

  /* Clean up and return error code CODE.  */
#define ERR(code) do { free (*xruns); return (code); } while (0)

  for (i = 0; i < num_runs; i++)
    {
      off_t addr = runs[i].start, left = runs[i].length;
      
      if (addr >= 0)
	for (j = 0; j < num_base_runs && left > 0; j++)
	  {
	    off_t baddr = base_runs[j].start;
	    off_t blen = base_runs[j].length;

	    if (addr >= blen)
	      addr -= blen;
	    else if (baddr < 0)
	      /* A hole, which is invalid.  */
	      ERR (EINVAL);
	    else
	      /* Add another output run.  */
	      {
		off_t len = blen - addr; /* Size of next output run.  */
		if (! add_run (baddr + addr, len > left ? left : len))
		  ERR (ENOMEM);
		addr = 0;
		left -= len;
	      }
	  }
      else
	/* a hole */
	if (! add_run (-1, left))
	  ERR (ENOMEM);
    }

  if (xruns_alloced > *num_xruns)
    *xruns = realloc (*xruns, *num_xruns * sizeof (struct store_run));

  return 0;
}

/* Return a store in STORE that reflects the blocks in RUNS & RUNS_LEN from
   SOURCE; SOURCE is consumed, but not RUNS.  Unlike the store_remap_create
   function, this may simply modify SOURCE and return it.  */
error_t
store_remap (struct store *source,
	     const struct store_run *runs, size_t num_runs,
	     struct store **store)
{
  if (source->class->remap)
    /* Use the class-specific remaping function.  */
    return (* source->class->remap) (source, runs, num_runs, store);
  else
    /* Just replace SOURCE's runs-list by an appropiately translated RUNS. */
    {
      struct store_run *xruns = 0;
      size_t num_xruns = 0;
      error_t err =
	store_remap_runs (runs, num_runs, source->runs, source->num_runs,
			  &xruns, &num_xruns);
      if (! err)
	{
	  /* Don't use store_set_runs because we've already allocated the
	     storages.  */
	  free (source->runs);
	  source->runs = xruns;
	  source->num_runs = num_xruns;
	  _store_derive (source);
	  *store = source;
	}
      return err;
    }
}
