/* Mach device store backend

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hurd.h>

#include "store.h"

static error_t
dev_read (struct store *store,
	  off_t addr, size_t index, mach_msg_type_number_t amount,
	  char **buf, mach_msg_type_number_t *len)
{
#if 1
  return device_read (store->port, 0, addr, amount, (io_buf_ptr_t *)buf, len);
#else
  char rep_buf[20];
  error_t err = device_read (store->port, 0, addr, amount, (io_buf_ptr_t *)buf, len);
  if (err)
    strcpy (rep_buf, "-");
  else if (*len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), *buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), *buf);
  fprintf (stderr, "; dev_read (%ld, %d, %d) [%d] => %s, %s, %d\n",
	   addr, index, amount, store->block_size, err ? strerror (err) : "-",
	   rep_buf, err ? 0 : *len);
  return err;
#endif
}

static error_t
dev_write (struct store *store,
	   off_t addr, size_t index, char *buf, mach_msg_type_number_t len,
	   mach_msg_type_number_t *amount)
{
#if 1
  return device_write (store->port, 0, addr, (io_buf_ptr_t)buf, len, amount);
#else
  error_t err;
  char rep_buf[20];
  if (len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), buf);
  err = device_write (store->port, 0, addr, (io_buf_ptr_t)buf, len, amount);
  fprintf (stderr, "; dev_write (%ld, %d, %s, %d) [%d] => %s, %d\n",
	   addr, index, rep_buf, len, store->block_size,
	   err ? strerror (err) : "-", *amount);
  return err;
#endif
}

static error_t
dev_decode (struct store_enc *enc, struct store_class *classes,
	    struct store **store)
{
  return store_std_leaf_decode (enc, _store_device_create, store);
}

static struct store_class
dev_class =
{
  STORAGE_DEVICE, "device", dev_read, dev_write,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, dev_decode
};
_STORE_STD_CLASS (dev_class);

/* Return a new store in STORE referring to the mach device DEVICE.  Consumes
   the send right DEVICE.  */
error_t
store_device_create (device_t device, int flags, struct store **store)
{
  struct store_run run;
  size_t sizes[DEV_GET_SIZE_COUNT], block_size;
  size_t sizes_len = DEV_GET_SIZE_COUNT;
  error_t err = device_get_status (device, DEV_GET_SIZE, sizes, &sizes_len);

  if (err)
    return err;

  assert (sizes_len == DEV_GET_SIZE_COUNT);

  block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
  run.start = 0;
  run.length = sizes[DEV_GET_SIZE_DEVICE_SIZE] / block_size;

  flags |= STORE_ENFORCED;	/* 'cause it's the whole device.  */

  return _store_device_create (device, flags, block_size, &run, 1, store);
}

/* Like store_device_create, but doesn't query the device for information.   */
error_t
_store_device_create (device_t device, int flags, size_t block_size,
		      const struct store_run *runs, size_t num_runs,
		      struct store **store)
{
  *store =
    _make_store (&dev_class, device, flags, block_size, runs, num_runs, 0);
  return *store ? 0 : ENOMEM;
}

/* Open the device NAME, and return the corresponding store in STORE.  */
error_t
store_device_open (const char *name, int flags, struct store **store)
{
  device_t dev_master, device;
  int open_flags = ((flags & STORE_HARD_READONLY) ? 0 : D_WRITE) | D_READ;
  error_t err = get_privileged_ports (0, &dev_master);

  if (err)
    return err;

  err = device_open (dev_master, open_flags, (char *)name, &device);
  mach_port_deallocate (mach_task_self (), dev_master);
  if (! err)
    {
      err = store_device_create (device, flags, store);
      if (err)
	mach_port_deallocate (mach_task_self (), device);
    }

  return err;
}
