/* Setting various store flags

   Copyright (C) 1996, 1997, 1998 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <malloc.h>
#include <string.h>

#include "store.h"

/* Add FLAGS to STORE's currently set flags.  */
error_t
store_set_flags (struct store *store, int flags)
{
  error_t err = 0;
  int orig = store->flags, new = flags & ~orig;

  if (new & STORE_BACKEND_FLAGS)
    {
      if (store->class->set_flags)
	err = (*store->class->set_flags) (store, new);
      else
	err = EINVAL;
    }

  if (! err)
    store->flags |= (new & ~STORE_BACKEND_FLAGS);

  return err;
}

/* Remove FLAGS from STORE's currently set flags.  */
error_t
store_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  int orig = store->flags, kill = flags & orig;

  if (kill & STORE_BACKEND_FLAGS)
    {
      if (store->class->clear_flags)
	err = (*store->class->clear_flags) (store, kill);
      else
	err = EINVAL;
    }

  if (! err)
    store->flags &= ~(kill & ~STORE_BACKEND_FLAGS);

  return err;
}
