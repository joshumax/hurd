/* store `device' I/O

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

#ifndef __DEV_H__
#define __DEV_H__

#include <mach.h>
#include <device/device.h>
#include <rwlock.h>
#include <hurd/store.h>

/* Information about a kernel device.  */
struct dev
{
  /* The device to which we're doing io.  */
  struct store *store;

  /* A bitmask corresponding to the part of an offset that lies within a
     device block.  */
  unsigned block_mask;

  /* Lock to arbitrate I/O through this device.  Block I/O can occur in
     parallel, and requires only a reader-lock.
     Non-block I/O is always serialized, and requires a writer-lock.  */
  struct rwlock io_lock;

  /* Non-block I/O is buffered through BUF.  BUF_OFFS is the device offset
     corresponding to the start of BUF (which holds one block); if it is -1,
     then BUF is inactive.  */
  void *buf;
  off_t buf_offs;
  int buf_dirty;

  struct pager *pager;
  struct mutex pager_lock;

  /* The current owner of the open device.  For terminals, this affects
     controlling terminal behavior (see term_become_ctty).  For all objects
     this affects old-style async IO.  Negative values represent pgrps.  This
     has nothing to do with the owner of a file (as returned by io_stat, and
     as used for various permission checks by filesystems).  An owner of 0
     indicates that there is no owner.  */
  pid_t owner;
};

/* Returns a pointer to a new device structure in DEV for the device
   NAME, with the given FLAGS.  If BLOCK_SIZE is non-zero, it should be the
   desired block size, and must be a multiple of the device block size.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t dev_open (struct store_parsed *name, int flags, struct dev **dev);

/* Free DEV and any resources it consumes.  */
void dev_close (struct dev *dev);

/* Returns in MEMOBJ the port for a memory object backed by the storage on
   DEV.  Returns 0 or the error code if an error occurred.  */
error_t dev_get_memory_object(struct dev *dev, memory_object_t *memobj);

/* Try to stop all paging activity on DEV, returning true if we were
   successful.  If NOSYNC is true, then we won't write back any (kernel)
   cached pages to the device.  */
int dev_stop_paging (struct dev *dev, int nosync);

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t dev_sync (struct dev *dev, int wait);

/* Write LEN bytes from BUF to DEV, returning the amount actually written in
   AMOUNT.  If successful, 0 is returned, otherwise an error code is
   returned.  */
error_t dev_write (struct dev *dev, off_t offs, void *buf, size_t len,
		   size_t *amount);

/* Read up to AMOUNT bytes from DEV, returned in BUF and LEN in the with the
   usual mach memory result semantics.  If successful, 0 is returned,
   otherwise an error code is returned.  */
error_t dev_read (struct dev *dev, off_t offs, size_t amount,
		  void **buf, size_t *len);

#endif /* !__DEV_H__ */
