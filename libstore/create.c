/* Store creation

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

#include <hurd/fs.h>

#include "store.h"

/* Return a new store in STORE, which refers to the storage underlying
   SOURCE.  A reference to SOURCE is created (but may be destroyed with
   store_close_source).  */
error_t store_create (file_t source, struct store **store)
{
  error_t err;
  mach_port_t port;
  string_t name_buf;
  char *misc;
  mach_msg_type_number_t misc_len = 0;
  off_t *runs;
  mach_msg_type_number_t runs_len = 0;
  int flags;
  size_t block_size;
  int class;

  err = file_get_storage_info (source, &class, &runs, &runs_len, &block_size,
			       name_buf, &port, &misc, &misc_len, &flags);
  if (err)
    return err;

  if (misc_len > 0)
    vm_deallocate (mach_task_self (), (vm_address_t)misc, misc_len);

  switch (class)
    {
    case STORAGE_DEVICE:
      err = _store_device_create (port, runs, runs_len, block_size, store);
      break;
    case STORAGE_HURD_FILE:
      err = _store_device_create (port, runs, runs_len, block_size, store);
      break;
    default:
      err = EINVAL;
    }

  if (!err && *name_buf)
    /* Copy out of our stack buffer NAME_BUF.  */
    store_set_name (*store, name_buf);

  if (runs_len > 0)
    /* RUNS is copied into malloced storage above.  */
    vm_deallocate (mach_task_self (), (vm_address_t)runs, runs_len);

  if (err)
    mach_port_deallocate (mach_task_self (), port);
  else
    /* Keep a reference to SOURCE around.  */
    {
      mach_port_mod_refs (mach_task_self (), source, MACH_PORT_RIGHT_SEND, 1);
      (*store)->source = source;
    }

  return err;
}
