/* Device input and output
   Copyright (C) 1992, 1993, 1994, 1995, 1996 Free Software Foundation, Inc.

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

#include <device/device.h>
#include <device/device_request.h>

#include <diskfs.h>

/* Write disk block ADDR with DATA of LEN bytes to DISKFS_DEVICE, waiting for
   completion.  ADDR is offset by DISKFS_DEVICE_START.  If an error occurs,
   EIO is returned.  */
error_t
diskfs_device_write_sync (off_t addr, vm_address_t data, size_t len)
{
  int written;
  error_t err;
  
  assert (!diskfs_readonly);
  err = device_write (diskfs_device, 0, diskfs_device_start + addr,
		      (io_buf_ptr_t) data, len, &written);
  
  if (err == D_READ_ONLY)
    return EROFS;
  else if (err || written != len)
    return EIO;
  return 0;
}

/* Read disk block ADDR from DISKFS_DEVICE; put the address of the data in
   DATA; read LEN bytes.  Always *DATA should be a full page no matter what.
   ADDR is offset by DISKFS_DEVICE_START.  If an error occurs, EIO is
   returned.  */
error_t
diskfs_device_read_sync (off_t addr, vm_address_t *data, size_t len)
{
  unsigned read;
  if (device_read (diskfs_device, 0, diskfs_device_start + addr, len,
		   (io_buf_ptr_t *)data, &read)
      || read != len)
    return EIO;
  return 0;
}

