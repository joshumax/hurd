/* Window management routines for buffered I/O using VM.

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

#ifndef __WINDOW_H__
#define __WINDOW_H__

#include <mach.h>

/* ---------------------------------------------------------------- */

/* A structure describing a memory window used to do buffered random access
   device i/o through the device pager.  */
struct window
{
  /* The currently allocated vm window backed by the device pager.  */
  vm_address_t buffer;

  /* The device offset of the window.  */
  vm_offset_t pos;
  /* The end of the device.  */
  vm_offset_t max_pos;

  /* The length of the window (should be a multiple of __vm_page_size).  If
     this is 0, this window isn't allocated.  */
  vm_size_t size;
  /* If SIZE < MIN_SIZE we won't shrink the window.  */
  vm_size_t min_size;
  /* If SIZE > MAX_SIZE, we'll try and shrink the window to fit.  */
  vm_size_t max_size;

  /* The device pager providing backing store for this window.  */
  mach_port_t memobj;
  /* True if the mapping should be read_only.  */
  int read_only;

  /* Lock this if you want to read/write some field(s) here.  */
  struct mutex lock;
};

/* Create a VM window onto the memory object MEMOBJ, and return it in WIN.
   MIN_SIZE and MAX_SIZE are the minimum and maximum sizes that the window
   will shrink/grow to.  */
error_t window_create(mach_port_t memobj, vm_offset_t max_pos,
		      vm_size_t min_size, vm_size_t max_size, int read_only,
		      struct window **win);

/* Free WIN and any resources it holds.  */
void window_free(struct window *win);

/* Write up to BUF_LEN bytes from BUF to the device that WIN is a window on,
   at offset *OFFS, using memory-mapped buffered I/O.  If successful, 0 is
   returned, otherwise an error code is returned.  *OFFS is incremented by
   the amount sucessfully written.  */
error_t window_write(struct window *win,
		     vm_address_t buf, vm_size_t buf_len, vm_size_t *amount,
		     vm_offset_t *offs);

/* Read up to AMOUNT bytes from the device that WIN is a window on, at offset
   *OFFS, into BUF and BUF_LEN (using the standard mach out-array
   conventions), using memory-mapped buffered I/O.  If successful, 0 is
   returned, otherwise an error code is returned.  *OFFS is incremented by
   the amount sucessfully written.  */
error_t window_read(struct window *win,
		    vm_address_t *buf, vm_size_t *buf_len,
		    vm_size_t amount, vm_offset_t *offs);

#endif /* !__WINDOW_H__ */
