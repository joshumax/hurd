/* Mach device store backend

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hurd.h>

#include "store.h"

static error_t
dev_read (struct store *store,
	  off_t addr, size_t index, mach_msg_type_number_t amount,
	  void **buf, mach_msg_type_number_t *len)
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
	   off_t addr, size_t index, void *buf, mach_msg_type_number_t len,
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
dev_decode (struct store_enc *enc, const struct store_class *const *classes,
	    struct store **store)
{
  return store_std_leaf_decode (enc, _store_device_create, store);
}

static error_t
dev_open (const char *name, int flags,
	  const struct store_class *const *classes,
	  struct store **store)
{
  return store_device_open (name, flags, store);
}

static error_t
dopen (const char *name, device_t *device, int *mod_flags)
{
  device_t dev_master;
  error_t err = get_privileged_ports (0, &dev_master);
  if (! err)
    {
      if (*mod_flags & STORE_HARD_READONLY)
	err = device_open (dev_master, D_READ, (char *)name, device);
      else
	{
	  err = device_open (dev_master, D_WRITE | D_READ, (char *)name, device);
	  if (err == ED_READ_ONLY)
	    {
	      err = device_open (dev_master, D_READ, (char *)name, device);
	      if (! err)
		*mod_flags |= STORE_HARD_READONLY;
	    }
	  else if (! err)
	    *mod_flags &= ~STORE_HARD_READONLY;
	}
      mach_port_deallocate (mach_task_self (), dev_master);
    }
  return err;
}

static void
dclose (struct store *store)
{
  mach_port_deallocate (mach_task_self (), store->port);
  store->port = MACH_PORT_NULL;
}

/* Return 0 if STORE's range is enforce by the kernel, otherwise an error.  */
static error_t
enforced (struct store *store)
{
  size_t sizes[DEV_GET_SIZE_COUNT];
  size_t sizes_len = DEV_GET_SIZE_COUNT;
  error_t err =
    device_get_status (store->port, DEV_GET_SIZE, sizes, &sizes_len);

  if (err)
    return err;

  assert (sizes_len == DEV_GET_SIZE_COUNT);

  if (sizes[DEV_GET_SIZE_RECORD_SIZE] != store->block_size
      || (store->runs[0].length !=
	  sizes[DEV_GET_SIZE_DEVICE_SIZE] >> store->log2_block_size))
    return EINVAL;

  return 0;
}

static error_t
dev_set_flags (struct store *store, int flags)
{
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    /* Trying to set flags we don't support.  */
    return EINVAL;

  if (! ((store->flags | flags) & STORE_INACTIVE))
    /* Currently active and staying that way, so we must be trying to set the
       STORE_ENFORCED flag.  */
    if (store->num_runs > 0 || store->runs[0].start != 0)
      /* Can't enforce non-contiguous ranges, or one not starting at 0.  */
      return EINVAL;
    else
      /* See if the the current (one) range is that the kernel is enforcing. */
      {
	error_t err = enforced (store);
	if (err)
	  return err;
      }

  if (flags & STORE_INACTIVE)
    dclose (store);

  store->flags |= flags;	/* When inactive, anything goes.  */

  return 0;
}

static error_t
dev_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    err = EINVAL;
  if (!err && (flags & STORE_INACTIVE))
    err = store->name ? dopen (store->name, &store->port, &store->flags) : ENODEV;
  if (! err)
    store->flags &= ~flags;
  return err;
}

struct store_class
store_device_class =
{
  STORAGE_DEVICE, "device", dev_read, dev_write,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, dev_decode,
  dev_set_flags, dev_clear_flags, 0, 0, 0, dev_open
};

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
  return
    _store_create (&store_device_class, device, flags, block_size,
		   runs, num_runs, 0, store);
}

/* Open the device NAME, and return the corresponding store in STORE.  */
error_t
store_device_open (const char *name, int flags, struct store **store)
{
  device_t device;
  error_t err = dopen (name, &device, &flags);
  if (! err)
    {
      err = store_device_create (device, flags, store);
      if (! err)
	{
	  err = store_set_name (*store, name);
	  if (err)
	    store_free (*store);
	}
      if (err)
	mach_port_deallocate (mach_task_self (), device);
    }
  return err;
}
