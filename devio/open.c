/* Per-open information for devio.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <hurd.h>

#include "open.h"
#include "window.h"
#include "dev.h"

/* ---------------------------------------------------------------- */

/* Returns a new per-open structure for the device DEV in OPEN.  If an error
   occurs, the error-code is returned, otherwise 0.  */
error_t
open_create(struct dev *dev, struct open **open)
{
  error_t err;

  *open = malloc(sizeof(struct open));

  if (*open == NULL)
    return ENOMEM;

  (*open)->dev = dev;

  err = io_state_init(&(*open)->io_state, dev);

  if (!err && dev_is(dev, DEV_BUFFERED) && !dev_is(dev, DEV_SERIAL))
    /* A random-access buffered device -- use a pager to do i/o to it.  */
    {
      mach_port_t memobj;
      err = dev_get_memory_object(dev, &memobj);
      if (!err)
	err =
	  window_create(memobj, 0, 0, dev_is(dev, DEV_READONLY),
			&(*open)->window); /* XXX sizes */
      if (err)
	{
	  mach_port_destroy(mach_task_self(), memobj);
	  io_state_finalize(&(*open)->io_state);
	}
    }
  else
    (*open)->window = NULL;

  if (err)
    free(*open);

  return err;
}

/* ---------------------------------------------------------------- */

/* Free OPEN and any resources it holds.  */
void
open_free(struct open *open)
{
  io_state_finalize(&open->io_state);
  window_free(open->window);
  free(open);
}


/* ---------------------------------------------------------------- */

/* Returns the appropiate io_state object for OPEN (which may be either
   per-open or a per-device depending on the device).  */
struct io_state *
open_get_io_state(struct open *open)
{
  return
    dev_is(open->dev, DEV_SERIAL) ? &open->dev->io_state : &open->io_state;
}
