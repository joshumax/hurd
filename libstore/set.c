/* Setting various store fields

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

/* Set STORE's current runs list to (a copy of) RUNS and NUM_RUNS.  */
error_t
store_set_runs (struct store *store, const off_t *runs, unsigned num_runs)
{
  unsigned size = num_runs * sizeof (off_t);
  off_t *copy = malloc (size);

  if (!copy)
    return ENOMEM;

  if (store->runs)
    free (store->runs);

  bcopy (runs, copy, size);
  store->runs = copy;
  store->num_runs = num_runs;

  if (store->block_size > 0)
    _store_derive (store);

  return 0;
}

/* Sets the name associated with STORE to a copy of NAME.  */
error_t
store_set_name (struct store *store, const char *name)
{
  char *copy = malloc (strlen (name) + 1);

  if (!copy)
    return ENOMEM;

  if (store->name)
    free (store->name);

  strcpy (copy, name);
  store->name = copy;

  return 0;
}

/* If STORE was created using store_create, remove the reference to the
   source from which it was created.  */
void store_close_source (struct store *store)
{
  if (store->source)
    {
      mach_port_deallocate (mach_task_self (), store->source);
      store->source = MACH_PORT_NULL;
    }
}
