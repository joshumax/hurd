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

#ifndef __IOSTATE_H__
#define __IOSTATE_H__

/* ---------------------------------------------------------------- */

enum io_state_buffer_use
{
  /* 0 means nothing */
  IO_STATE_BUFFERED_WRITE = 1, IO_STATE_BUFFERED_READ = 2
};

struct io_state {
  /* What we think the current position is.  */
  vm_offset_t location;

  /* The buffer in which we accumulate buffered i/o.  */
  vm_address_t buffer;
  /* The size of BUFFER.  */
  vm_size_t buffer_size;

  /* If LOCATION is not a multiple of the block size (and so points somewhere
     in the middle of BUFFER), this indicates why.  */
  enum io_state_buffer_use buffer_use;

  /* Lock this if you want to read/modify LOCATION or BUFFER.  */
  struct mutex lock;
};

#define io_state_lock(ios) mutex_lock(&(ios)->lock)
#define io_state_unlock(ios) mutex_unlock(&(ios)->lock)

/* Declare this to keep the parameter scope below sane.  */
struct dev;

/* Initialize the io_state structure IOS to be used with the device DEV.  If
   a memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
error_t io_state_init(struct io_state *ios, struct dev *dev);

/* Frees all resources used by IOS.  */
void io_state_finalize(struct io_state *ios);

/* If IOS's location isn't block aligned because writes have been buffered
   there, then sync the whole buffer out to the device.  Any error that
   occurs while writing is returned, otherwise 0.  */
error_t io_state_sync(struct io_state *ios, struct dev *dev);

#endif /* !__IOSTATE_H__ */
