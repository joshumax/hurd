/* Store wire decoding

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

#include <string.h>
#include <malloc.h>

#include "store.h"

/* Decodes the standard leaf encoding that's common to various builtin
   formats, and calls CREATE to actually create the store.  */
error_t
store_default_leaf_decode (struct store_enc *enc,
			   error_t (*create)(mach_port_t port,
					     size_t block_size,
					     const off_t *runs,
					     size_t num_runs,
					     struct store **store),
			   struct store **store)
{
  char *misc;
  error_t err;
  int type, flags;
  mach_port_t port;
  size_t block_size, num_runs, name_len, misc_len;
  
  /* Make sure there are enough encoded ints and ports.  */
  if (enc->cur_int + 6 > enc->num_ints || enc->cur_port + 1 > enc->num_ports)
    return EINVAL;

  /* Read encoded ints.  */
  type = enc->ints[enc->cur_int++];
  flags = enc->ints[enc->cur_int++];
  block_size = enc->ints[enc->cur_int++];
  num_runs = enc->ints[enc->cur_int++];
  name_len = enc->ints[enc->cur_int++];
  misc_len = enc->ints[enc->cur_int++];

  /* Make sure there are enough encoded offsets and data.  */
  if (enc->cur_offset + num_runs * 2 > enc->num_offsets
      || enc->cur_data + name_len + misc_len > enc->data_len)
    return EINVAL;

  if (enc->data[enc->cur_data + name_len - 1] != '\0')
    return EINVAL;		/* Name not terminated.  */

  misc = malloc (misc_len);
  if (! misc)
    return ENOMEM;

  /* Read encoded ports (be careful to deallocate this if we barf).  */
  port = enc->ports[enc->cur_port++];

  err =
    (*create)(port, block_size, enc->offsets + enc->cur_offset, num_runs,
	      store);

  if (err)
    {
      mach_port_deallocate (mach_task_self (), port);
      free (misc);
    }
  else
    {
      (*store)->flags = flags;
      (*store)->misc = misc;
      (*store)->misc_len = misc_len;
    }

  return err;
}

/* Decode ENC, either returning a new store in STORE, or an error.  If
   nothing else is to be done with ENC, its contents may then be freed using
   store_enc_dealloc.  */
error_t
store_decode (struct store_enc *enc, struct store **store)
{
  if (enc->cur_int >= enc->num_ints)
    /* The first int should always be the type.  */
    return EINVAL;

  switch (enc->ints[enc->cur_int])
    {
    case STORAGE_HURD_FILE:
      return store_default_leaf_decode (enc, _store_file_create, store);
    case STORAGE_DEVICE:
      return store_default_leaf_decode (enc, _store_device_create, store);
#if 0
    case STORAGE_TASK:
    case STORAGE_MEMORY:

    case STORAGE_ILEAVE:
      return store_ileave_decode (enc, store);
    case STORAGE_CONCAT:
      return store_concat_decode (enc, store);
    case STORAGE_LAYER:
      return store_layer_decode (enc, store);
    case STORAGE_NULL:
      return store_null_decode (enc, store);
#endif

    default:
      return EINVAL;
    }
}
