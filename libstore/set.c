/* Setting various store fields

   Copyright (C) 1995,96,97,2001,02 Free Software Foundation, Inc.
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
#include <errno.h>
#include <mach.h>

#include "store.h"

/* Set STORE's current runs list to (a copy of) RUNS and NUM_RUNS.  */
error_t
store_set_runs (struct store *store,
		const struct store_run *runs, size_t num_runs)
{
  unsigned size = num_runs * sizeof (struct store_run);
  struct store_run *copy = malloc (size);

  if (!copy)
    return ENOMEM;

  if (store->runs)
    free (store->runs);

  memcpy (copy, runs, size);
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
  char *copy = strdup (name);

  if (!copy)
    return ENOMEM;

  if (store->name)
    free (store->name);

  store->name = copy;

  return 0;
}

/* If STORE was created using store_create, remove the reference to the
   source from which it was created.  */
void store_close_source (struct store *store)
{
  if (store->source != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), store->source);
      store->source = MACH_PORT_NULL;
    }
}
