/* Store cloning

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
#include <stdlib.h>

#include "store.h"

/* Return in TO a copy of FROM.  */
error_t
store_clone (struct store *from, struct store **to)
{
  error_t err = 0;
  struct store *c =
    _make_store (from->class, from->meths, from->port, from->block_size,
		 from->runs, from->num_runs, from->end);

  if (! c)
    return ENOMEM;

  c->flags = from->flags;

  if (from->misc_len)
    {
      c->misc = malloc (from->misc_len);
      if (! c->misc)
	err = ENOMEM;
    }

  if (!err && c->port != MACH_PORT_NULL)
    {
      err = mach_port_mod_refs (mach_task_self (),
				c->port, MACH_PORT_RIGHT_SEND, 1);
      if (err)
	c->port = MACH_PORT_NULL; /* Don't deallocate it.  */
    }
  if (!err && from->source != MACH_PORT_NULL)
    {
      err = mach_port_mod_refs (mach_task_self (),
				from->source, MACH_PORT_RIGHT_SEND, 1);
      if (! err)
	c->source = from->source;
    }
  if (!err && from->num_children > 0)
    {
      int k;

      c->children = malloc (from->num_children * sizeof (struct store *));
      if (! c->children)
	err = ENOMEM;

      for (k = 0; !err && k < from->num_children; k++)
	{
	  err = store_clone (from->children[k], &c->children[k]);
	  if (! err)
	    c->num_children++;
	}
    }

  if (!err && from->meths->clone)
    err = (*from->meths->clone)(from, c);

  if (err)
    store_free (c);
  else
    *to = c;

  return err;
}
