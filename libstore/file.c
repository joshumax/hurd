/* Mach file store backend

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#include <assert.h>

#include "store.h"

static error_t
file_read (struct store *store, off_t addr, mach_msg_type_number_t amount,
	  char **buf, mach_msg_type_number_t *len)
{
  return io_read (store->port, buf, len, addr * store->block_size, amount);
}

static error_t
file_write (struct store *store,
	   off_t addr, char *buf, mach_msg_type_number_t len,
	   mach_msg_type_number_t *amount)
{
  return io_write (store->port, buf, len, addr * store->block_size, amount);
}

static struct store_meths
file_meths = {dev_read, dev_write};

/* Return a new store in STORE referring to the mach file FILE.  Consumes
   the send right FILE.  */
error_t
store_file_create (file_t file, struct store **store)
{
  off_t runs[2];
  size_t sizes[DEV_GET_SIZE_COUNT], block_size;
  unsigned sizes_len = DEV_GET_SIZE_COUNT;
  error_t err = file_get_status (file, DEV_GET_SIZE, sizes, &sizes_len);

  if (err)
    return err;

  assert (sizes_len == DEV_GET_SIZE_COUNT);

  block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
  runs[0] = 0;
  runs[1] = sizes[DEV_GET_SIZE_FILE_SIZE] / block_size;

  return _store_file_create (file, runs, 2, block_size, store);
}

/* Like store_file_create, but doesn't query the file for information.   */
error_t
_store_file_create (file_t file,
		      off_t *runs, unsigned runs_len, size_t block_size,
		      struct store **store)
{
  *store = _make_store (STORAGE_FILE, &file_meths, file, block_size,
			runs, runs_len);
  return *store ? 0 : ENOMEM;
}
