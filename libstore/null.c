/* Null store backend

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

static error_t
null_read (struct store *store,
	   off_t addr, size_t index, mach_msg_type_number_t amount,
	   char **buf, mach_msg_type_number_t *len)
{
  if (*len < amount)
    {
      error_t err =
	vm_allocate (mach_task_self (), (vm_address_t *)buf, amount, 1);
      if (! err)
	*len = amount;
      return err;
    }
  else
    {
      bzero (*buf, amount);
      *len = amount;
      return 0;
    }
}

static error_t
null_write (struct store *store,
	   off_t addr, size_t index, char *buf, mach_msg_type_number_t len,
	   mach_msg_type_number_t *amount)
{
  return 0;
}

error_t
null_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  enc->num_ints += 2;
  enc->num_offsets += 1;
  return 0;
}

error_t
null_encode (const struct store *store, struct store_enc *enc)
{
  enc->ints[enc->cur_int++] = store->class->id;
  enc->ints[enc->cur_int++] = store->flags;
  enc->offsets[enc->cur_offset++] = store->size;
  return 0;
}

error_t
null_decode (struct store_enc *enc, struct store_class *classes,
	     struct store **store)
{
  off_t size;
  int type, flags;

  if (enc->cur_int + 2 > enc->num_ints
      || enc->cur_offset + 1 > enc->num_offsets)
    return EINVAL;

  type = enc->ints[enc->cur_int++];
  flags = enc->ints[enc->cur_int++];
  size = enc->offsets[enc->cur_offset++];

  return store_null_create (size, flags, store);
}

static struct store_class
null_class =
{
  STORAGE_NULL, "null", null_read, null_write,
  null_allocate_encoding, null_encode, null_decode
};
_STORE_STD_CLASS (null_class);

/* Return a new null store SIZE bytes long in STORE.  */
error_t
store_null_create (size_t size, int flags, struct store **store)
{
  struct store_run run = { 0, size };
  *store = _make_store (&null_class, MACH_PORT_NULL, flags, 1, &run, 1, 0);
  return *store ? 0 : ENOMEM;
}
