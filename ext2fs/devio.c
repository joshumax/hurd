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
dev_write_sync (daddr_t addr, vm_address_t data, long len)
{
  int written;
  assert (!diskfs_readonly);
  if (device_write (device_port, 0, addr, (io_buf_ptr_t) data, len, &written)
      || written != len)
    return EIO;
  return 0;
}

/* Read disk block ADDR; put the address of the data in DATA; read LEN
   bytes.  Always *DATA should be a full page no matter what.   */
error_t
dev_read_sync (daddr_t addr, vm_address_t *data, long len)
{
  u_int read;
  if (device_read (device_port, 0, addr, len, (io_buf_ptr_t *)data, &read)
      || read != len)
    return EIO;
  return 0;
}

