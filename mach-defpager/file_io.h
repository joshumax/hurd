/* Backing store access callbacks for Hurd version of Mach default pager.
   Copyright (C) 2000 Free Software Foundation, Inc.

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

#ifndef _file_io_h
#define _file_io_h 1

/* The original Mach default pager code used in serverboot can read
   filesystem meta-data to find the blocks used by paging files.
   We replace those interfaces with simpler code that only supports
   subsets of devices represented by a list of runs a la libstore.  */

#include <sys/types.h>

#include <device/device_types.h>
#include <device/device.h>

/* A run of device records, expressed in the device's record size.  */
struct storage_run
{
  recnum_t start, length;
};

struct file_direct
{
  mach_port_t device;

  int bshift;			/* size of device records (disk blocks) */
  size_t fd_bsize;		/* log2 of that */
  recnum_t fd_size;		/* number of blocks total */

  /* The paging area consists of the concatentation of NRUNS contiguous
     regions of the device, as described by RUNS.  */
  size_t nruns;
  struct storage_run runs[0];
};

/* These are in fact only called to read or write a single page, from
   default_pager.c::default_read/default_write.  The SIZE argument is
   always vm_page_size and OFFSET is always page-aligned.  */

int page_read_file_direct (struct file_direct *fdp,
			   vm_offset_t offset,
			   vm_size_t size,
			   vm_offset_t *addr,		/* out */
			   vm_size_t *size_read);	/* out */
int page_write_file_direct(struct file_direct *fdp,
			   vm_offset_t offset,
			   vm_offset_t addr,
			   vm_size_t size,
			   vm_size_t *size_written);	/* out */


#endif /* file_io.h */
