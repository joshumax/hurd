/* Standard device global variables

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

/* A mach device port for the device we're using.  */
mach_port_t diskfs_device = MACH_PORT_NULL;

/* The mach device name of DISKFS_DEVICE.  May be 0 if unknown.  */
char *diskfs_device_name = 0;

/* The first valid block of DISKFS_DEVICE, in units of
   DISKFS_DEVICE_BLOCK_SIZE.  */
off_t diskfs_device_start = 0;

/* The usable size of DISKFS_DEVICE, in units of DISKFS_DEVICE_BLOCK_SIZE.  */
off_t diskfs_device_size = 0;

/* The unit of addressing for DISKFS_DEVICE.  */ 
unsigned diskfs_device_block_size = 0;

/* Some handy calculations based on DISKFS_DEVICE_BLOCK_SIZE.  */
/* Log base 2 of DEVICE_BLOCK_SIZE, or 0 if it's not a power of two.  */
unsigned diskfs_log2_device_block_size = 0;
/* Log base 2 of the number of device blocks in a vm page, or 0 if it's not a
   power of two.  */
unsigned diskfs_log2_device_blocks_per_page = 0;
