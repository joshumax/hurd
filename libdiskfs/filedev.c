/* Get the mach device underlying a file

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

#include <errno.h>
#include <sys/fcntl.h>

#include <device/device.h>

#include "priv.h"

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
  int i;
  error_t err;
  mach_msg_type_number_t data_len = 100;
  mach_msg_type_number_t num_ints = 10, num_ports = 10, num_offsets = 10;
  int _ints[num_ints], *ints = _ints;
  mach_port_t _ports[num_ports], *ports = _ports;
  off_t _offsets[num_offsets], *offsets = _offsets;
  char _data[data_len], *data = _data;
  file_t node =
    file_name_lookup (name, diskfs_readonly ? O_RDONLY : O_RDWR, 0);

  if (node == MACH_PORT_NULL)
    return errno;

  *port = MACH_PORT_NULL;

  err = file_get_storage_info (node, &ports, &num_ports, &ints, &num_ints,
			       &offsets, &num_offsets, &data, &data_len);
  if (err)
    return err;

  /* See <hurd/store.h> for an explanation of what's in the vectors returned
     by file_get_storage_info.  */

  if (num_ints < 6)
    err = EGRATUITOUS;
  else if (ints[0] != STORAGE_DEVICE)
    err = ENODEV;

  if (!err && block_size)
    *block_size = ints[2];

  if (!err && (start || size))
    /* Extract the device block addresses.  */
    {
      size_t num_runs = ints[3];
      if (num_runs != 1)
	/* We can't handle anything but a contiguous set of blocks.  */
	err = ENODEV;		/* XXX */
      else
	{
	  if (start)
	    *start = offsets[0];
	  if (size)
	    *size = offsets[1];
	}
    }

  if (!err && dev_name)
    /* Extract the device name into DEV_NAME.  */
    {
      size_t name_len = ints[4];
      if (data && name_len > 0)
	if (data_len < name_len)
	  err = EGRATUITOUS;
	else
	  {
	    *dev_name = malloc (name_len);
	    if (*dev_name)
	      strcpy (*dev_name, data);
	    else
	      err = ENOMEM;
	  }
    }

  if (!err && port)
    /* Extract the device port.  */
    if (num_ports != 1)
      err = EGRATUITOUS;
    else
      *port = ports[0];

  /* Deallocate things we don't care about or that we've made copies of.  */

  for (i = 0; i < num_ports; i++)
    if (MACH_PORT_VALID (ports[i]))
      mach_port_deallocate (mach_task_self (), ports[i]);

#define DISCARD_MEM(v, vl, b)						    \
  if (v != b)							   	    \
    vm_deallocate (mach_task_self (), (vm_address_t)v, vl * sizeof *v);
  DISCARD_MEM (ports, num_ports, _ports);
  DISCARD_MEM (ints, num_ints, _ints);
  DISCARD_MEM (offsets, num_offsets, _offsets);
  DISCARD_MEM (data, data_len, _data);

  /* Note that we don't deallocate NODE unless we're returning an error,
     which should prevent the information returned by file_get_storage_info
     from changing.  */
  
  if (err)
    /* We got an error, deallocate everything.  */
    mach_port_deallocate (mach_task_self (), node);

  return err;
}
