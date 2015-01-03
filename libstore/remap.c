/* Block address translation

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
#include <ctype.h>
#include <hurd/fs.h>

#include "store.h"

static error_t
remap_read (struct store *store,
	     store_offset_t addr, size_t index, size_t amount,
	     void **buf, size_t *len)
{
  return store_read (store->children[0], addr, amount, buf, len);
}

static error_t
remap_write (struct store *store,
	      store_offset_t addr, size_t index, const void *buf, size_t len,
	      size_t *amount)
{
  return store_write (store->children[0], addr, buf, len, amount);
}

static error_t
remap_set_size (struct store *store, size_t newsize)
{
  return store_set_size (store->children[0], newsize);
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
      int type __attribute__((unused)) = enc->ints[enc->cur_int++];
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

error_t
remap_open (const char *name, int flags,
	    const struct store_class *const *classes,
	    struct store **store)
{
  error_t err;
  struct store *from;
  const char *end, *p;
  struct store_run *runs;
  size_t nruns;

  end = strchr (name, ':');
  if (!end)
    return EINVAL;

  runs = alloca ((end - name) * sizeof runs[0]);

  nruns = 0;
  p = name;
  do
    {
      char *endp;
      runs[nruns].start = strtoul (p, &endp, 0);
      if (*endp == '+')
	{
	  if (endp == p)	/* Syntax "+5,+7" means "0+5,0+7".  */
	    runs[nruns].start = 0;
	  p = endp + 1;
	  if (p == end || *p == ',')
	    {
	      /* Syntax "100+" means block 100 to the end of the store.
		 Since we don't know the size yet, we use -1 as a marker
		 for the code below.  */
	      runs[nruns++].length = (store_offset_t) -1;
	      break;
	    }
	  runs[nruns].length = strtoul (p, &endp, 0);
	  if (endp == p)
	    return EINVAL;
	}
      else if (endp == p)	/* Must have a number unless starts with +. */
	return EINVAL;
      else
	runs[nruns].length = 1;
      ++nruns;
      p = endp;
      if (*p == ',')
	++p;
    } while (p < end);

  err = store_typed_open (end + 1, flags, classes, &from);
  if (!err)
    {
      /* Check for any runs marked as "through the end of the store"
	 and update them to use the actual size of the store.  */
      size_t i;
      for (i = 0; i < nruns; ++i)
	if (runs[i].length == (store_offset_t) -1)
	  runs[i].length = from->blocks - runs[i].start;

      /* Now do the remapping according to RUNS.  */
      err = store_remap (from, runs, nruns, store);
      if (err)
	store_free (from);
    }
  return err;
}

error_t
remap_validate_name (const char *name,
		     const struct store_class *const *classes)
{
  const char *end = strchr (name, ':');
  const char *p;

  if (!end)
    return EINVAL;

  p = name;
  do
    {
      if (*p != '+')
	{
	  if (!isdigit (*p))
	    return EINVAL;
	  do
	    ++p;
	  while (isdigit (*p));
	}

      if (*p == '+')
	{
	  ++p;
	  if (!isdigit (*p))
	    return EINVAL;
	  do
	    ++p;
	  while (isdigit (*p));
	}

      if (*p == ',')
	++p;
      else if (*p == ':')
	return 0;
    } while (*p != '\0');

  return EINVAL;
}


const struct store_class
store_remap_class =
{
  STORAGE_REMAP, "remap", remap_read, remap_write, remap_set_size,
  remap_allocate_encoding, remap_encode, remap_decode,
  store_set_child_flags, store_clear_child_flags,
  NULL, NULL, NULL,		/* cleanup, clone, remap */
  remap_open, remap_validate_name
};
STORE_STD_CLASS (remap);

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
  error_t err =
    _store_create (&store_remap_class, MACH_PORT_NULL, flags | source->flags,
		   source->block_size, runs, num_runs, 0, store);

  if (! err)
    {
      err = store_set_children (*store, &source, 1);
      if (err)
	store_free (*store);
    }

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
  int add_run (store_offset_t addr, store_offset_t len)
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
      store_offset_t addr = runs[i].start, left = runs[i].length;

      if (addr >= 0)
	for (j = 0; j < num_base_runs && left > 0; j++)
	  {
	    store_offset_t baddr = base_runs[j].start;
	    store_offset_t blen = base_runs[j].length;

	    if (addr >= blen)
	      addr -= blen;
	    else if (baddr < 0)
	      /* A hole, which is invalid.  */
	      ERR (EINVAL);
	    else
	      /* Add another output run.  */
	      {
		store_offset_t len = blen - addr; /* Size of next output run.  */
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
    /* Just replace SOURCE's runs-list by an appropriately translated RUNS. */
    {
      struct store_run *xruns = 0;
      size_t num_xruns = 0;
      error_t err =
	store_remap_runs (runs, num_runs, source->runs, source->num_runs,
			  &xruns, &num_xruns);
      if (! err)
	{
	  /* Don't use store_set_runs -- we've already allocated the
	     storage. */
	  free (source->runs);
	  source->runs = xruns;
	  source->num_runs = num_xruns;
	  source->flags &= ~STORE_ENFORCED;
	  source->end = 0;	/* Needed to make _store_derive work.  */
	  store_close_source (source);
	  _store_derive (source);
	  *store = source;
	}
      return err;
    }
}
