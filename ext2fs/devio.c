/* Device input and output
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "ext2fs.h"
#include <device/device.h>
#include <device/device_request.h>

/* Write disk block ADDR with DATA of LEN bytes, waiting for completion.  */
error_t
dev_write_sync (daddr_t addr,
		vm_address_t data,
		long len)
{
  int foo;
  assert (!diskfs_readonly);
  if (device_write (ext2fs_device, 0, addr, (io_buf_ptr_t) data, len, &foo)
      || foo != len)
    return EIO;
  return 0;
}

/* Write diskblock ADDR with DATA of LEN bytes; don't bother waiting
   for completion. */
error_t
dev_write (daddr_t addr,
	   vm_address_t data,
	   long len)
{
  assert (!diskfs_readonly);
  if (device_write_request (ext2fs_device, MACH_PORT_NULL, 0, addr,
			    (io_buf_ptr_t) data, len))
    return EIO;
  return 0;
}

static int deverr;

/* Read disk block ADDR; put the address of the data in DATA; read LEN
   bytes.  Always *DATA should be a full page no matter what.   */
error_t
dev_read_sync (daddr_t addr,
	       vm_address_t *data,
	       long len)
{
  int foo;
  deverr = device_read (ext2fs_device, 0, addr, len, (io_buf_ptr_t *)data,
			(u_int *)&foo);
  if (deverr || foo != len)
    return EIO;
  return 0;
}

