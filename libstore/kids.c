/* Managing sub-stores

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

#include <malloc.h>
#include <string.h>

#include "store.h"

/* Set STORE's current children list to (a copy of) CHILDREN and NUM_CHILDREN.  */
error_t
store_set_children (struct store *store,
		    struct store *const *children, unsigned num_children)
{
  unsigned size = num_children * sizeof (struct store_run);
  struct store **copy = malloc (size);

  if (!copy)
    return ENOMEM;

  if (store->children)
    free (store->children);

  bcopy (children, copy, size);
  store->children = copy;
  store->num_children = num_children;

  return 0;
}

/* Calls the allocate_encoding method in each child store of STORE,
   propagating any errors.  If any child does not hae such a method,
   EOPNOTSUPP is returned.  */
error_t
store_allocate_child_encodings (const struct store *store,
				struct store_enc *enc)
{
  int i;
  error_t err = 0;
  for (i = 0; i < store->num_children && !err; i++)
    {
      struct store *k = store->children[i];
      if (k->class->allocate_encoding)
	(*k->class->allocate_encoding) (store, enc);
      else
	err = EOPNOTSUPP;
    }
  return err;
}

/* Calls the encode method in each child store of STORE, propagating any
   errors.  If any child does not hae such a method, EOPNOTSUPP is returned. */
error_t
store_encode_children (const struct store *store, struct store_enc *enc)
{
  int i;
  error_t err = 0;
  for (i = 0; i < store->num_children && !err; i++)
    {
      struct store *k = store->children[i];
      if (k->class->encode)
	(*k->class->encode) (store, enc);
      else
	err = EOPNOTSUPP;
    }
  return err;
}

/* Decodes NUM_CHILDREN from ENC, storing the results into successive
   positions in CHILDREN.  */
error_t
store_decode_children (struct store_enc *enc, int num_children,
		       struct store_class *classes, struct store **children)
{
  int i;
  error_t err = 0;
  for (i = 0; i < num_children && !err; i++)
    err = store_decode (enc, classes, &children[i]);
  if (err)
    /* Deallocate anything we've already created.  */
    while (--i >= 0)
      store_free (children[i]);
  return err;
}
