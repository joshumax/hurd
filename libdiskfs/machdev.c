/* Get mach device info

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

#include <stdio.h>

#include <device/device.h>

#include "priv.h"

/* Returns a send right to for the mach device called NAME, and returns it in
   PORT.  Other values returned are START, the first valid offset, SIZE, the
   the number of blocks after START, and BLOCK_SIZE, the units in which the
   device is addressed.

   The device is opened for reading, and if the diskfs global variable
   DISKFS_READ_ONLY is false, writing.

   If NAME cannot be opened and this is a bootstrap filename, the user will
   be prompted for new names until a valid one is found.  */
error_t
diskfs_get_mach_device (char *name,
			mach_port_t *port,
			off_t *start, off_t *size, size_t *block_size)
{
  error_t err = 0;

  do
    {
      mach_port_t dev_master;
      
      err = get_privileged_ports (0, &dev_master);
      if (err)
	return err;

      err = device_open (dev_master, 
			 (diskfs_readonly ? 0 : D_WRITE) | D_READ,
			 name, port);

      if (err == D_NO_SUCH_DEVICE && diskfs_boot_flags)
	/* If this is a bootstrap filesystem, prompt the user to give us
	   another name rather than just crashing.  */
	{
	  char *line = 0;
	  size_t linesz = 0;
	  ssize_t len;

	  printf ("Cannot open device %s\n", name);
	  printf ("Open instead: ");
	  fflush (stdout);
	  len = getline (&line, &linesz, stdin);
	  if (len > 2)
	    name = line;
	}

      mach_port_deallocate (mach_task_self (), dev_master);
    }
  while (err == D_NO_SUCH_DEVICE && diskfs_boot_flags);

  if (!err)
    {
      unsigned sizes_len = DEV_GET_SIZE_COUNT;
      size_t sizes[DEV_GET_SIZE_COUNT];

      err = device_get_status (*port, DEV_GET_SIZE, sizes, &sizes_len);
      assert (sizes_len == DEV_GET_SIZE_COUNT);

      *start = 0;
      *size = sizes[DEV_GET_SIZE_DEVICE_SIZE];
      *block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
      if (*block_size > 1)
	*size /= *block_size;
    }

  return err;
}
