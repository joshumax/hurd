/* State for an I/O stream.

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

#include "iostate.h"
#include "dev.h"

/* ---------------------------------------------------------------- */

/* Initialize the io_state structure IOS to be used with the device DEV.  If
   a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t
io_state_init(struct io_state *ios, struct dev *dev)
{
  error_t err =
    vm_allocate(mach_task_self(),
		(vm_address_t *)&ios->buffer, dev->block_size, 1);

  ios->location = 0;
  ios->buffer_size = dev->block_size;
  ios->buffer_use = 0;
  mutex_init(&ios->lock);

  return err;
}

/* Frees all resources used by IOS.  */
void
io_state_finalize(struct io_state *ios)
{
  vm_deallocate(mach_task_self(), (vm_address_t)ios->buffer, ios->buffer_size);
}

/* If IOS's location isn't block aligned because writes have been buffered
   there, then sync the whole buffer out to the device.  Any error that
   occurs while writing is returned, otherwise 0.  */
error_t
io_state_sync(struct io_state *ios, struct dev *dev)
{
  error_t err = 0;

  if (ios->buffer_use == IO_STATE_BUFFERED_WRITE)
    {
      vm_offset_t pos = ios->location;
      int block_offs = pos % dev->block_size;

      if (block_offs > 0)
	{
	  bzero((char *)ios->buffer + block_offs,
		dev->block_size - block_offs);
	  ios->location -= block_offs;
	  err =
	    dev_write(dev, ios->buffer, dev->block_size, &ios->location);
	}

      /* Remember that there's nothing left in the buffer.  */
      ios->buffer_use = 0;
    }

  return err;
}
