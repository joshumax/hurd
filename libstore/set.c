/* Setting various store fields

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include "store.h"

/* Set STORE's current runs list to (a copy of) RUNS and RUNS_LEN.  */
error_t
store_set_runs (struct store *store, off_t *runs, unsigned runs_len)
{
  off_t *copy = malloc (runs_len * sizeof (off_t));

  if (!copy)
    return ENOMEM;

  if (store->runs)
    free (store->runs);

  store->runs = copy;
  store->runs_len = runs_len;

  return 0;
}

/* Sets the name associated with STORE to a copy of NAME.  */
error_t
store_set_name (struct store *store, char *name)
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
