/* Store wire encoding

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

#include "store.h"

/* Default encoding used for most leaf store types.  */

error_t
store_default_leaf_allocate_encoding (struct store *store,
				      struct store_enc *enc)
{
  enc->ports_len++;
  enc->ints_len += 6;
  enc->offsets_len += store->runs_len;
  if (store->name)
    enc->data_len += strlen (store->name) + 1;
  enc->data_len += store->misc_len;
  return 0;
}

error_t
store_default_leaf_encode (struct store *store, struct store_enc *enc)
{
  size_t name_len = (store->name ? strlen (store->name) + 1 : 0);

  enc->ports[enc->cur_port++] = store->port;

  enc->ints[enc->cur_int++] = store->class;
  enc->ints[enc->cur_int++] = store->flags;
  enc->ints[enc->cur_int++] = store->block_size;
  enc->ints[enc->cur_int++] = store->runs_len;
  enc->ints[enc->cur_int++] = name_len;
  enc->ints[enc->cur_int++] = store->misc_len;

  for (i = 0; i < store->runs_len; i++)
    enc->offsets[enc->cur_offset++] = store->runs[i];

  if (store->name)
    {
      bcopy (store->name, enc->data + enc->cur_data, name_len);
      enc->cur_data += name_len;
    }
  if (store->misc_len)
    {
      bcopy (store->misc, enc->data + enc->cur_data, store->misc_len);
      enc->cur_data += store->misc_len;
    }

  return 0;
}

/* Encode STORE into ENC, which should have been prepared with
   store_enc_init, or return an error.  The contents of ENC may then be
   return as the value of file_get_storage_info; if for some reason this
   can't be done, store_enc_dealloc may be used to deallocate the mmemory
   used by the unsent vectors.  */
error_t
store_encode (const struct store *store, struct store_enc *enc)
{
  error_t err;
  struct store_meths *meths = store->meths;
  /* We zero each vector length for the allocate_encoding method to work, so
     save the old values.  */
  mach_msg_type_number_t init_ports_len = enc->ports_len;
  mach_msg_type_number_t init_ints_len = enc->ints_len;
  mach_msg_type_number_t init_offsets_len = enc->offsets_len;
  mach_msg_type_number_t init_data_len = enc->data_len;

  if (!meths->allocate_encoding || !meths->encoding)
    return EOPNOTSUPP;

  enc->ports_len = 0;
  enc->ints_len = 0;
  enc->offsets_len = 0;
  enc->data_len = 0;
  err = (*meths->allocate_encoding) (store, enc);
  if (err)
    return err;

  if (enc->ports_len > init_ports_len)
    err = vm_allocate (mach_task_self (),
		       (vm_address_t *)&enc->ports, enc->ports_len, 1);
  if (!err && enc->ints_len > init_ints_len)
    err = vm_allocate (mach_task_self (),
		       (vm_address_t *)&enc->ints, enc->ints_len, 1);
  if (!err && enc->offsets_len > init_offsets_len)
    err = vm_allocate (mach_task_self (),
		       (vm_address_t *)&enc->offsets, enc->offsets_len, 1);
  if (!err && enc->data_len > init_data_len)
    err = vm_allocate (mach_task_self (),
		       (vm_address_t *)&enc->data, enc->data_len, 1);

  if (! err)
    err = (*meths->encode) (store, enc);

  enc->cur_port = enc->cur_int = enc->cur_offset = enc->cur_data = 0;

  if (err)
    store_enc_dealloc (enc);

  return err;
}
