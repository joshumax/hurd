/* Get the mach device underlying a file

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

#include <errno.h>
#include <sys/fcntl.h>

#include <device/device.h>

#include "priv.h"

typedef int run_elem_t;

/* Returns the name and a send right for the mach device on which the file
   NAME is stored, and returns it in DEV_NAME (which is malloced) and PORT.
   Other values returned are START, the first valid offset, SIZE, the the
   number of blocks after START, and BLOCK_SIZE, the units in which the
   device is addressed.

   The device is opened for reading, and if the diskfs global variable
   DISKFS_READONLY is false, writing.  */
error_t
diskfs_get_file_device (char *name,
			char **dev_name, mach_port_t *port,
			off_t *start, off_t *size, size_t *block_size)
{
  error_t err;
  int class, flags;
  char *misc = 0;
  off_t *runs = 0;
  mach_msg_type_number_t misc_len = 0, runs_len = 0;
  string_t dev_name_buf;
  file_t node =
    file_name_lookup (name, diskfs_readonly ? O_RDONLY : O_RDWR, 0);

  if (node == MACH_PORT_NULL)
    return errno;

  *port = MACH_PORT_NULL;

  err = file_get_storage_info (node, &class, &runs, &runs_len, block_size,
			       dev_name_buf, port, &misc, &misc_len, &flags);
  if (err)
    goto done;

  if (class != STORAGE_DEVICE)
    {
      err = ENODEV;
      goto done;
    }

  if (dev_name)
    {
      *dev_name = malloc (strlen (dev_name_buf) + 1);
      if (*dev_name)
	strcpy (*dev_name, dev_name_buf);
      else
	err = ENOMEM;
    }

  if (start || size)
    if (runs_len > 2)
      /* We can't handle anything but a contiguous set of blocks.  */
      err = ENODEV;		/* XXX */
    else
      {
	if (start)
	  *start = runs[0];
	if (size)
	  *size = runs[1];
      }

  /* Note that we don't deallocate NODE, which should prevent the information
     returned by file_get_storage_info from changing.  */

 done:
  if (err)
    {
      mach_port_deallocate (mach_task_self (), node);
      if (*port)
	mach_port_deallocate (mach_task_self (), *port);
    }

  if (misc_len > 0)
    vm_deallocate (mach_task_self (), (vm_address_t)misc, misc_len);
  if (runs_len > 0)
    vm_deallocate (mach_task_self (),
		   (vm_address_t)runs, runs_len * sizeof (*runs));
  
  return err;
}
