/* Per-open information for storeio

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#include "dev.h"

/* ---------------------------------------------------------------- */

/* A structure describing a particular i/o stream on this device.  */
struct open
{
  /* The device that this an open on.  */
  struct dev *dev;

  /* The per-open offset used for I/O operations that don't specify an
     explicit offset.  */
  off_t offs;

  /* A lock used to control write access to OFFS.  */
  pthread_mutex_t lock;
};

/* Returns a new per-open structure for the device DEV in OPEN.  If an error
   occurs, the error-code is returned, otherwise 0.  */
error_t open_create (struct dev *dev, struct open **open);

/* Free OPEN and any resources it holds.  */
void open_free (struct open *open);

/* Writes up to LEN bytes from BUF to OPEN's device at device offset OFFS
   (which may be ignored if the device doesn't support random access),
   and returns the number of bytes written in AMOUNT.  If no error occurs,
   zero is returned, otherwise the error code is returned.  */
error_t open_write (struct open *open, off_t offs, void *buf, size_t len,
		    size_t *amount);

/* Reads up to AMOUNT bytes from the device into BUF and BUF_LEN using the
   standard mach out-array convention.  If no error occurs, zero is returned,
   otherwise the error code is returned.  */
error_t open_read (struct open *open, off_t offs, size_t amount,
		   void **buf, size_t *buf_len);

/* Set OPEN's location to OFFS, interpreted according to WHENCE as by seek.
   The new absolute location is returned in NEW_OFFS (and may not be the same
   as OFFS).  If no error occurs, zero is returned, otherwise the error code
   is returned.  */
error_t open_seek (struct open *open, off_t offs, int whence, off_t *new_offs);

#endif /* !__OPEN_H__ */
