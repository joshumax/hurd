/* Store allocation/deallocation

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

struct store *
_make_store (enum file_storage_class class, struct store_meths *meths)
{
  struct store *store = malloc (sizeof (struct store));
  if (store)
    {
      store->name = 0;
      store->port = MACH_PORT_NULL;
      store->runs = 0;
      store->runs_len = 0;
      store->block_size = 0;
      store->source = MACH_PORT_NULL;
      store->blocks = 0;
      store->size = 0;
      store->log2_block_size = 0;
      store->log2_blocks_per_page = 0;

      store->class = class;
      store->meths = meths;
    }
  return store;
}

void
_store_free (struct store *store)
{
  if (store->port)
    mach_port_deallocate (mach_task_self (), store->port);
  if (store->source)
    mach_port_deallocate (mach_task_self (), store->source);
  if (store->name)
    free (store->name);
  if (store->runs)
    free (store->runs);
  free (store);
}
