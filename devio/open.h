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

#ifndef __OPEN_H__
#define __OPEN_H__

#include "iostate.h"

/* ---------------------------------------------------------------- */

/* A structure describing a particular i/o stream on this device.  */
struct open
{
  /* Current state of our output stream -- location and the buffer used to do
     buffered i/o.  */
  struct io_state io_state;

  /* The memory window we're using to do i/o.  This may be NULL, indicating
     we're not doing buffered random access i/o.  */
  struct window *window;

  /* The device that this an open on.  */
  struct dev *dev;
};

/* Returns a new per-open structure for the device DEV in OPEN.  If an error
   occurs, the error-code is returned, otherwise 0.  */
error_t open_create(struct dev *dev, struct open **open);

/* Free OPEN and any resources it holds.  */
void open_free(struct open *open);

/* Returns the appropiate io_state object for OPEN (which may be either
   per-open or a per-device depending on the device).  */
struct io_state *open_get_io_state(struct open *open);

/* Writes up to LEN bytes from BUF to OPEN's device at device offset OFFS
   (which may be ignored if the device doesn't support random access),
   and returns the number of bytes written in AMOUNT.  If no error occurs,
   zero is returned, otherwise the error code is returned.  */
error_t open_write(struct open *open, vm_address_t buf, vm_size_t len,
		   vm_size_t *amount, off_t offs);

/* Reads up to AMOUNT bytes from the device into BUF and BUF_LEN using the
   standard mach out-array convention.  If no error occurs, zero is returned,
   otherwise the error code is returned.  */
error_t open_read(struct open *open, vm_address_t *buf, vm_size_t *buf_len,
		  vm_size_t amount, off_t offs);

#endif /* !__OPEN_H__ */
