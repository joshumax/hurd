/* Store creation

   Copyright (C) 1995,96,97,2001 Free Software Foundation, Inc.
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

#include <hurd/fs.h>

#include "store.h"

/* Return a new store in STORE, which refers to the storage underlying SOURCE.
   CLASSES is used to select classes specified by the provider; if it is 0,
   STORE_STD_CLASSES is used.  FLAGS is set with store_set_flags, with the
   exception of STORE_INACTIVE, which merely indicates that no attempt should
   be made to activate an inactive store; if STORE_INACTIVE is not specified,
   and the store returned for SOURCE is inactive, an attempt is made to
   activate it (failure of which causes an error to be returned).  A reference
   to SOURCE is created (but may be destroyed with store_close_source).  */
error_t
store_create (file_t source, int flags,
	      const struct store_class *const *classes,
	      struct store **store)
{
  error_t err;
  struct store_enc enc;
  mach_port_t inline_ports[10];
  int inline_ints[60];
  off_t inline_offsets[60];
  char inline_data[100];

  store_enc_init (&enc, inline_ports, 10, inline_ints, 60,
		  inline_offsets, 60, inline_data, 100);

  err = file_get_storage_info (source,
			       &enc.ports, &enc.num_ports,
			       &enc.ints, &enc.num_ints,
			       &enc.offsets, &enc.num_offsets,
			       &enc.data, &enc.data_len);
  if (err)
    return err;

  err = store_decode (&enc, classes, store);
  if (! err)
    {
      if (flags & STORE_INACTIVE)
	flags &= ~STORE_INACTIVE; /* Don't actually make store inactive.  */
      else if ((*store)->flags & STORE_INACTIVE)
	err = store_clear_flags (*store, STORE_INACTIVE);
      if (!err && flags)
	err = store_set_flags (*store, flags);
      if (err)
	store_free (*store);
    }
  else if (err == EINVAL && (flags &~ STORE_INACTIVE) == STORE_NO_FILEIO)
    /* Open a generic "unknown" store that can regurgitate this encoding.  */
    err = store_unknown_decode (&enc, classes, store);

  store_enc_dealloc (&enc);

  if (! err)
    /* Keep a reference to SOURCE around.  */
    (*store)->source = source;

  return err;
}
