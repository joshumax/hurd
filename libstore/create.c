/* Store creation

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

#include <hurd/fs.h>

#include "store.h"

static error_t
fgsi (file_t source,
      mach_port_t **ports, mach_msg_type_number_t *num_ports,
      int **ints, mach_msg_type_number_t *num_ints,
      off_t **offsets, mach_msg_type_number_t *num_offsets,
      char **data, mach_msg_type_number_t *num_data)
{
  return EOPNOTSUPP;
}

/* Return a new store in STORE, which refers to the storage underlying
   SOURCE.  A reference to SOURCE is created (but may be destroyed with
   store_close_source).  */
error_t store_create (file_t source, struct store **store)
{
  error_t err;
  struct store_enc enc;
  mach_port_t inline_ports[10];
  int inline_ints[60];
  off_t inline_offsets[60];
  char inline_data[100];

  store_enc_init (&enc, inline_ports, 10, inline_ints, 60,
		  inline_offsets, 60, inline_data, 100);

#define file_get_storage_info fgsi /* XXX */
  err = file_get_storage_info (source,
			       &enc.ports, &enc.num_ports,
			       &enc.ints, &enc.num_ints,
			       &enc.offsets, &enc.num_offsets,
			       &enc.data, &enc.data_len);
  if (err)
    return err;

  err = store_decode (&enc, store);

  store_enc_dealloc (&enc);

  if (! err)
    /* Keep a reference to SOURCE around.  */
    {
      mach_port_mod_refs (mach_task_self (), source, MACH_PORT_RIGHT_SEND, 1);
      (*store)->source = source;
    }

  return err;
}
