/* Standard device opening

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <diskfs.h>

/* Uses the values of DISKFS_DEVICE_ARG and DISKFS_USE_MACH_DEVICE, and
   attempts to open the device and set the values of DISKFS_DEVICE,
   DISKFS_DEVICE_NAME, DISKFS_DEVICE_START, DISKFS_DEVICE_SIZE, and
   DISKFS_DEVICE_BLOCK_SIZE.  */
error_t
diskfs_device_open ()
{
  error_t err;
  if (diskfs_use_mach_device)
    {
      diskfs_device_name = diskfs_device_arg;
      err = 
	diskfs_get_mach_device (diskfs_device_name, &diskfs_device,
				&diskfs_device_start, &diskfs_device_size,
				&diskfs_device_block_size);
    }
  else
    err =
      diskfs_get_file_device (diskfs_device_arg,
			      &diskfs_device_name, &diskfs_device,
			      &diskfs_device_start, &diskfs_device_size,
			      &diskfs_device_block_size);

  if (! err)
    {
      diskfs_log2_device_block_size = 0;
      while ((1 << diskfs_log2_device_block_size) < diskfs_device_block_size)
	diskfs_log2_device_block_size++;
      while ((1 << diskfs_log2_device_block_size) != diskfs_device_block_size)
	diskfs_log2_device_block_size = 0;

      diskfs_log2_device_blocks_per_page = 0;
      while ((diskfs_device_block_size << diskfs_log2_device_blocks_per_page)
	     < vm_page_size)
	diskfs_log2_device_blocks_per_page++;
      if ((diskfs_device_block_size << diskfs_log2_device_blocks_per_page)
	  != vm_page_size)
	diskfs_log2_device_blocks_per_page = 0;
    }

  return err;
}
