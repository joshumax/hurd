/* A handle on a mach device.

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

#ifndef __DEV_H__
#define __DEV_H__

#include <mach.h>
#include <device/device.h>

#include "iostate.h"

/* #define FAKE */
#define MSG

#ifdef MSG
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern FILE *debug;
extern struct mutex debug_lock;
#endif

/* ---------------------------------------------------------------- */

/* Information about a kernel device.  */
struct dev
{
  /* The device port for the kernel device we're doing paging on.  */
  device_t port;

  /* The total size of DEV.  */
  vm_size_t size;

  /* The block size of DEV.  I/O to DEV must occur in multiples of
     block_size.  */
  int dev_block_size;
  /* The block size in which we will do I/O; this must be a multiple of
     DEV_BLOCK_SIZE.  */
  int block_size;

  /* Various attributes of this device (see below for the DEV_ flag bits).
     This field is constant.  */
  int flags;

  /* Current state of our output stream -- location and the buffer used to do
     buffered i/o.  */
  struct io_state io_state;

  /* The pager we're using to do disk i/o for us.  If NULL, a pager hasn't
     been allocated yet.  Lock the lock in IO_STATE if you want to update
     this field.  */
  struct pager *pager;
  /* The port_bucket for paging ports.  */
  struct port_bucket *pager_port_bucket;

  /* The current owner of the open device.  For terminals, this affects
     controlling terminal behavior (see term_become_ctty).  For all objects
     this affects old-style async IO.  Negative values represent pgrps.  This
     has nothing to do with the owner of a file (as returned by io_stat, and
     as used for various permission checks by filesystems).  An owner of 0
     indicates that there is no owner.  */
  pid_t owner;
};

/* Various bits to be set in the flags field.  */

/* True if this device should be used in `block' mode, with buffering of
   sub-block-size i/o.  */
#define DEV_BUFFERED	0x1
/* True if this device only supports serial i/o (that is, there's only one
   read/write location, which must explicitly be moved to do i/o elsewhere.*/
#define DEV_SERIAL	0x2
/* True if we can change the current i/o location of a serial device.  */
#define DEV_SEEKABLE	0x4
/* True if a device cannot be written on.  */
#define DEV_READONLY	0x8

/* Returns TRUE if any of the flags in BITS are set for DEV.  */
#define dev_is(dev, bits) ((dev)->flags & (bits))

/* Returns true if it's ok to call dev_write on these arguments, without
   first copying BUF to a page-aligned buffer.  */
#define dev_write_valid(dev, buf, len, offs) \
  ((len) <= IO_INBAND_MAX || (buf) % vm_page_size == 0)

/* Returns a pointer to a new device structure in DEV for the kernel device
   NAME, with the given FLAGS.  If BLOCK_SIZE is non-zero, it should be the
   desired block size, and must be a multiple of the device block size.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t dev_open(char *name, int flags, int block_size, struct dev **dev);

/* Free DEV and any resources it consumes.  */
void dev_close(struct dev *dev);

/* Reads AMOUNT bytes from DEV and returns it in BUF and BUF_LEN (using the
   standard mach out-array convention).  *OFFS is incremented to reflect the
   amount read/written.  Both LEN and *OFFS must be multiples of DEV's block
   size.  If an error occurs, the error code is returned, otherwise 0.  */
error_t dev_read(struct dev *dev,
		 vm_address_t *buf, vm_size_t *buf_len, vm_size_t amount,
		 vm_offset_t *offs);

/* Writes AMOUNT bytes from the buffer pointed to by BUF to the device DEV.
   *OFFS is incremented to reflect the amount read/written.  Both AMOUNT and
   *OFFS must be multiples of DEV's block size, and either BUF must be
   page-aligned, or dev_write_valid() must return true for these arguments.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t dev_write(struct dev *dev,
		  vm_address_t buf, vm_size_t amount, vm_offset_t *offs);

/* Returns in MEMOBJ the port for a memory object backed by the storage on
   DEV.  Returns 0 or the error code if an error occurred.  */
error_t dev_get_memory_object(struct dev *dev, memory_object_t *memobj);

/* Try to stop all paging activity on DEV, returning true if we were
   successful.  If NOSYNC is true, then we won't write back any (kernel)
   cached pages to the device.  */
int dev_stop_paging (struct dev *dev, int nosync);

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t dev_sync(struct dev *dev, int wait);

#ifdef MSG
char *brep(vm_address_t buf, vm_size_t len);
#endif

#endif /* !__DEV_H__ */
