/* Get the disklabel from a device node

   Copyright (C) 1996,99,2001 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
#include <hurd.h>
#include <mach.h>
#include <string.h>
#include <device/device.h>
#include <device/disk_status.h>
#include <hurd/store.h>

/* XXX Ick. */
#define IOCPARM_MASK 0x1fff/* parameter length, at most 13 bits */
#define IOC_OUT 0x40000000/* copy out parameters */
#define _IOC(inout,group,num,len) \
	(inout | ((len & IOCPARM_MASK) << 16) | ((group) << 8) | (num))
#define _IOR(g,n,t) _IOC(IOC_OUT,(g), (n), sizeof(t))

static error_t
fd_get_device (int fd, device_t *device)
{
  error_t err;
  struct store *store;
  file_t node = getdport (fd);

  if (node == MACH_PORT_NULL)
    return errno;

  err = store_create (node, 0, 0, &store); /* consumes NODE on success */
  if (! err)
    {
      if (store->class->id != STORAGE_DEVICE
	  /* In addition to requiring a device, we also want the *whole*
	     device -- one contiguous run starting at 0.  */
	  || store->num_runs != 1
	  || store->runs[0].start != 0)
	err = ENODEV;
      else if (store->port == MACH_PORT_NULL)
	/* Usually getting a null port back means we didn't have sufficient
	   privileges.  */
	err = EPERM;
      else
	{
	  *device = store->port;
	  store->port = MACH_PORT_NULL;	/* Steal the port from STORE!  */
	}
      store_free (store);
    }
  else
    mach_port_deallocate (mach_task_self (), node);

  return err;
}

error_t
fd_get_disklabel (int fd, struct disklabel *label)
{
  device_t device;
  error_t err = fd_get_device (fd, &device);

  if (! err)
    {
      mach_msg_type_number_t label_len = sizeof *label / sizeof (integer_t);

      err = device_get_status (device, DIOCGDINFO,
			       (dev_status_t)label, &label_len);
      if (!err && label_len != sizeof *label / sizeof (integer_t))
	err = ERANGE;

      mach_port_deallocate (mach_task_self (), device);
    }

  return err;
}
