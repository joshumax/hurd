/* Store allocation/deallocation

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
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

#include "store.h"

/* Allocate a new store structure with meths METHS, and the various other
   fields initialized to the given parameters.  */
error_t
_store_create (const struct store_class *class,
	       mach_port_t port, int flags, size_t block_size,
	       const struct store_run *runs, size_t num_runs,
	       store_offset_t end, struct store **store)
{
  if ((block_size & (block_size - 1)) || (block_size == 0 && num_runs > 0))
    return EINVAL;		/* block size not a power of two.  */
  else
    {
      struct store *new = malloc (sizeof (struct store));
      if (new)
	{
	  error_t err;

	  new->name = 0;
	  new->port = port;
	  new->runs = 0;
	  new->num_runs = 0;
	  new->wrap_src = 0;
	  new->wrap_dst = 0;
	  new->flags = flags;
	  new->end = end;
	  new->block_size = block_size;
	  new->source = MACH_PORT_NULL;
	  new->blocks = 0;
	  new->size = 0;
	  new->log2_block_size = 0;
	  new->log2_blocks_per_page = 0;
	  new->misc = 0;
	  new->misc_len = 0;
	  new->hook = 0;
	  new->children = 0;
	  new->num_children = 0;

	  new->class = class;

	  /* store_set_runs calls _store_derive to derive other fields. */
	  err = store_set_runs (new, runs, num_runs);
	  if (err)
	    free (new);
	  else
	    *store = new;

	  return err;
	}
      else
	return ENOMEM;
    }
}

void
store_free (struct store *store)
{
  int k;

  if (store->class->cleanup)
    (*store->class->cleanup) (store);

  for (k = 0; k < store->num_children; k++)
    store_free (store->children[k]);

  if (store->port != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), store->port);
  if (store->source != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), store->source);

  if (store->name)
    free (store->name);
  if (store->runs)
    free (store->runs);

  free (store);
}
