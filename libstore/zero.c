/* Zero store backend

   Copyright (C) 1995,96,97,99,2000,01, 02 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>

#include "store.h"

static error_t
zero_read (struct store *store,
	   store_offset_t addr, size_t index, size_t amount, void **buf,
	   size_t *len)
{
  if (*len < amount)
    {
      *buf = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*buf == MAP_FAILED)
	return errno;
      *len = amount;
      return 0;
    }
  else
    memset (*buf, 0, amount);

  *len = amount;
  return 0;
}

static error_t
zero_write (struct store *store,
	    store_offset_t addr, size_t index, const void *buf, size_t len,
	    size_t *amount)
{
  return 0;
}

static error_t
zero_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
}

/* Modify SOURCE to reflect those runs in RUNS, and return it in STORE.  */
error_t
zero_remap (struct store *source,
	    const struct store_run *runs, size_t num_runs,
	    struct store **store)
{
  /* Because all blocks are the same, a zero store always contains just one
     run; here we simply count up the number of blocks specified by RUNS, and
     modify SOURCE's one run to reflect that.  */
  int i;
  store_offset_t length = 0, old_length = source->runs[0].length;
  for (i = 0; i < num_runs; i++)
    if (runs[i].start < 0 || runs[i].start + runs[i].length >= old_length)
      return EINVAL;
    else
      length += runs[i].length;
  source->runs[0].length = length;
  *store = source;
  return 0;
}

error_t
zero_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  enc->num_ints += 2;
  enc->num_offsets += 1;
  return 0;
}

error_t
zero_encode (const struct store *store, struct store_enc *enc)
{
  enc->ints[enc->cur_int++] = store->class->id;
  enc->ints[enc->cur_int++] = store->flags;
  enc->offsets[enc->cur_offset++] = store->size;
  return 0;
}

error_t
zero_decode (struct store_enc *enc, const struct store_class *const *classes,
	     struct store **store)
{
  store_offset_t size;
  int flags;

  if (enc->cur_int + 2 > enc->num_ints
      || enc->cur_offset + 1 > enc->num_offsets)
    return EINVAL;

  /* Ignore type.  */
  enc->cur_int++;
  flags = enc->ints[enc->cur_int++];
  size = enc->offsets[enc->cur_offset++];

  return store_zero_create (size, flags, store);
}

static error_t
zero_open (const char *name, int flags,
	   const struct store_class *const *classes,
	   struct store **store)
{
  if (name)
    {
      char *end;
      store_offset_t size = strtoull (name, &end, 0);
      if (end == name || end == NULL)
	return EINVAL;
      switch (*end)
	{
	case 'b':
	  size *= 512;
	  break;
	case 'k':
	case 'K':
	  size *= 1024;
	  break;
	case 'm':
	case 'M':
	  size *= 1024 * 1024;
	  break;
	case 'g':
	case 'G':
	  size *= 1024 * 1024 * 1024;
	  break;
	}
      return store_zero_create (size, flags, store);
    }
  else
    {
      store_offset_t max_offs = ~((store_offset_t)1
				  << (CHAR_BIT * sizeof (store_offset_t) - 1));
      return store_zero_create (max_offs, flags, store);
    }
}

static error_t
zero_validate_name (const char *name, const struct store_class *const *classes)
{
  if (name)
    {
      char *end;
      strtoul (name, &end, 0);
      return end == name ? EINVAL : 0;
    }
  else
    return 0;			/* `maximum size' */
}

static error_t
zero_map (const struct store *store, vm_prot_t prot, mach_port_t *memobj)
{
  *memobj = MACH_PORT_NULL;
  return 0;
}

const struct store_class
store_zero_class =
{
  STORAGE_ZERO, "zero", zero_read, zero_write, zero_set_size,
  zero_allocate_encoding, zero_encode, zero_decode,
  0, 0, 0, 0, zero_remap, zero_open, zero_validate_name,
  zero_map
};
STORE_STD_CLASS (zero);

/* Return a new zero store SIZE bytes long in STORE.  */
error_t
store_zero_create (store_offset_t size, int flags, struct store **store)
{
  struct store_run run = { 0, size };
  return
    _store_create (&store_zero_class, MACH_PORT_NULL,
		   flags | STORE_INNOCUOUS, 1, &run, 1, 0, store);
}
