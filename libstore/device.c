/* Mach device backend

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

#include "store.h"

/* Return a new store in STORE referring to the mach device DEVICE.  Consumes
   the send right DEVICE.  */
error_t
store_device_create (device_t device, struct store **store)
{
  off_t runs[2];
  size_t sizes[DEV_GET_SIZE_COUNT], block_size;
  unsigned sizes_len = DEV_GET_SIZE_COUNT;
  error_t err = device_get_status (*port, DEV_GET_SIZE, sizes, &sizes_len);

  assert (sizes_len == DEV_GET_SIZE_COUNT);

  block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
  runs[0] = 0;
  runs[1] = sizes[DEV_GET_SIZE_DEVICE_SIZE] / block_size;

  return _store_device_create (device, runs, 2, block_size, store);
}

/* Like store_device_create, but doesn't query the device for information.   */
error_t
_store_device_create (device_t device,
		      off_t *runs, unsigned runs_len, size_t block_size,
		      struct store **store)
{
  *store = _alloc_store ();

  if (!*store)
    return ENOMEM;

  (*store)->block_size = block_size;
  store_set_runs (*store, runs, runs_len);
  store_derive (*store);

  return 0;
}
