/* Mach device store backend

   Copyright (C) 1995,96,97,99,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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

#include <assert-backtrace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hurd.h>

#include "store.h"

static inline error_t
dev_error (error_t err)
{
  /* Give the canonical POSIX error codes,
     rather than letting the Mach code propagate up.  */
  switch (err)
    {
    case D_IO_ERROR:		return EIO;
    case D_WOULD_BLOCK:		return EAGAIN;
    case D_NO_SUCH_DEVICE:	return ENXIO;
    case D_ALREADY_OPEN:	return EBUSY;
    case D_DEVICE_DOWN: 	return ENXIO; /* ? */
    case D_INVALID_OPERATION:	return EBADF; /* ? */
    case D_NO_MEMORY:		return ENOMEM;
    default:
      break;
    }
  /* Anything unexpected propagates up where weirdness will get noticed.  */
  return err;
}

static error_t
dev_read (struct store *store,
	  store_offset_t addr, size_t index, mach_msg_type_number_t amount,
	  void **buf, mach_msg_type_number_t *len)
{
  return dev_error (device_read (store->port, 0, addr, amount,
				 (io_buf_ptr_t *)buf, len));
}

static error_t
dev_write (struct store *store,
	   store_offset_t addr, size_t index,
	   const void *buf, mach_msg_type_number_t len,
	   mach_msg_type_number_t *amount)
{
  error_t err = dev_error (device_write (store->port, 0, addr,
					 (io_buf_ptr_t)buf, len,
					 (int *) amount));
  *amount = *(int *) amount;	/* stupid device.defs uses int */
  return err;
}

static error_t
dev_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
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
  return dev_error (store_device_open (name, flags, store));
}

static error_t
dopen (const char *name, device_t *device, int *mod_flags)
{
  device_t dev_master;
  error_t err = ENODEV;

  if (name[0] == '/')
    {
      if (*mod_flags & STORE_HARD_READONLY)
	{
	  dev_master = file_name_lookup (name, O_READ, 0);
	  if (dev_master != MACH_PORT_NULL)
	    {
	      err = device_open (dev_master, D_READ, "disk", device);
	      if (err)
		err = ENODEV;

	      mach_port_deallocate (mach_task_self (), dev_master);
	    }
	  else
	    err = ENODEV;
	}
      else
	{
	  dev_master = file_name_lookup (name, O_READ | O_WRITE, 0);
	  if (dev_master != MACH_PORT_NULL)
	    {
	      err = device_open (dev_master, D_READ | D_WRITE, "disk", device);
	      if (err == ED_READ_ONLY)
		{
		  err = device_open (dev_master, D_READ, "disk", device);
		  if (! err)
		    *mod_flags |= STORE_HARD_READONLY;
		  else
		    err = ENODEV;
		}
	      else if (! err)
		*mod_flags &= ~STORE_HARD_READONLY;

	      mach_port_deallocate (mach_task_self (), dev_master);
	    }
	  else
	    err = ENODEV;
	}
    }

  if (err)
    {
      err = get_privileged_ports (0, &dev_master);
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
  error_t err;
  dev_status_data_t sizes;
  size_t sizes_len = DEV_STATUS_MAX;

  if (store->num_runs != 1 || store->runs[0].start != 0)
    /* Can't enforce non-contiguous ranges, or one not starting at 0.  */
    return EINVAL;
  else
    /* See if the the current (one) range is that the kernel is enforcing. */
    {
#ifdef DEV_GET_RECORDS
      err =
	device_get_status (store->port, DEV_GET_RECORDS, sizes, &sizes_len);

      if (err && err != D_INVALID_OPERATION)
	return EINVAL;

      if (!err)
	{
	  assert_backtrace (sizes_len == DEV_GET_RECORDS_COUNT);

	  if (sizes[DEV_GET_RECORDS_RECORD_SIZE] != store->block_size
	      || (store->runs[0].length !=
		  sizes[DEV_GET_RECORDS_DEVICE_RECORDS]))
	    return EINVAL;

	  return 0;
	}
      else
#endif
	{
	  sizes_len = DEV_STATUS_MAX;
	  err =
	    device_get_status (store->port, DEV_GET_SIZE, sizes, &sizes_len);

	  if (err)
	    return EINVAL;

	  assert_backtrace (sizes_len == DEV_GET_SIZE_COUNT);

	  if (sizes[DEV_GET_SIZE_RECORD_SIZE] != store->block_size
	      || (store->runs[0].length !=
		  sizes[DEV_GET_SIZE_DEVICE_SIZE] >> store->log2_block_size))
	    return EINVAL;

	  return 0;
	}
    }
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

static error_t
dev_map (const struct store *store, vm_prot_t prot, mach_port_t *memobj)
{
  size_t nruns = store->num_runs;

  if (nruns > 1 || (nruns == 1 && store->runs[0].start != 0))
    return EOPNOTSUPP;
  else
    {
      /* Note that older Mach drivers (through GNU Mach 1.x) ignore
	 the OFFSET and SIZE parameters.  The OSKit-Mach drivers obey
	 them, and so the size we pass must be large enough (or zero
	 only if the size is indeterminable).  If using only the newer
	 drivers, we could remove the `start != 0' condition above and
	 support kernel mapping of partial devices.  However, since
	 the older drivers silently ignore the OFFSET argument, that
	 would produce scrambled results on old kernels.  */
      error_t err = device_map (store->port, prot,
				store->runs[0].start,
				store->runs[0].length,
				memobj, 0);
      if (err == ED_INVALID_OPERATION)
	err = EOPNOTSUPP;	/* This device doesn't support paging.  */
      return err;
    }
}

const struct store_class
store_device_class =
{
  STORAGE_DEVICE, "device", dev_read, dev_write, dev_set_size,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, dev_decode,
  dev_set_flags, dev_clear_flags, 0, 0, 0, dev_open, 0, dev_map
};
STORE_STD_CLASS (device);

/* Return a new store in STORE referring to the mach device DEVICE.  Consumes
   the send right DEVICE.  */
error_t
store_device_create (device_t device, int flags, struct store **store)
{
  struct store_run run;
  size_t block_size = 0;
  dev_status_data_t sizes;
  size_t sizes_len = DEV_STATUS_MAX;
  error_t err;

#ifdef DEV_GET_RECORDS
  err = device_get_status (device, DEV_GET_RECORDS, sizes, &sizes_len);
  if (! err && sizes_len == DEV_GET_RECORDS_COUNT)
    {
      block_size = sizes[DEV_GET_RECORDS_RECORD_SIZE];

      if (block_size)
	{
	  run.start = 0;
	  run.length = sizes[DEV_GET_RECORDS_DEVICE_RECORDS];
	}
    }
  else
#endif
    {
      /* Some Mach devices do not implement device_get_status, but do not
	 return an error.  To detect these devices we set the size of the
	 input buffer to something larger than DEV_GET_SIZE_COUNT.  If the
	 size of the returned device status is not equal to
	 DEV_GET_SIZE_COUNT, we know that something is wrong.  */
      sizes_len = DEV_STATUS_MAX;
      err = device_get_status (device, DEV_GET_SIZE, sizes, &sizes_len);
      if (! err && sizes_len == DEV_GET_SIZE_COUNT)
	{
	  block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];

	  if (block_size)
	    {
	      run.start = 0;
	      run.length = sizes[DEV_GET_SIZE_DEVICE_SIZE] / block_size;

	      if (run.length * block_size != sizes[DEV_GET_SIZE_DEVICE_SIZE])
		/* Bogus results (which some mach devices return).  */
		block_size = 0;
	    }
	}
    }

  flags |= STORE_ENFORCED;	/* 'cause it's the whole device.  */

  if (block_size == 0)
    /* Treat devices that can't do device_get_status as zero-length.  */
    return _store_device_create (device, flags, 0, &run, 0, store);
  else
    /* Make a store with one run covering the whole device.  */
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
