/* File store backend

   Copyright (C) 1995,96,97,98,2001, 2002 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <hurd.h>

#include <hurd/io.h>

#include "store.h"

/* Return 0 if STORE's range is enforced by the filesystem, otherwise an
   error.  */
static error_t
enforced (struct store *store)
{
  if (store->num_runs != 1 || store->runs[0].start != 0)
    /* Can't enforce non-contiguous ranges, or one not starting at 0.  */
    return EINVAL;
  else
    {
      /* See if the the current (one) range is that the kernel is enforcing. */
      struct stat st;
      error_t err = io_stat (store->port, &st);

      if (!err
	  && store->runs[0].length != (st.st_size >> store->log2_block_size))
	/* The single run is not the whole file.  */
	err = EINVAL;

      return err;
    }
}

static error_t
file_read (struct store *store,
	   store_offset_t addr, size_t index, size_t amount, void **buf,
	   size_t *len)
{
  size_t bsize = store->block_size;
  return io_read (store->port, (char **)buf, len, addr * bsize, amount);
}

static error_t
file_write (struct store *store,
	    store_offset_t addr, size_t index, const void *buf, size_t len,
	    size_t *amount)
{
  size_t bsize = store->block_size;
  return io_write (store->port, (void *) buf, len, addr * bsize, amount);
}

static error_t
file_store_set_size (struct store *store, size_t newsize)
{
  error_t err;

  if (enforced (store) != 0)
    /* Bail out if there is more than a single run.  */
    return EOPNOTSUPP;

  err = file_set_size (store->port, newsize);

  if (!err)
  {
    /* Update STORE's size and run.  */
    store->size = newsize;
    store->runs[0].length = newsize >> store->log2_block_size;
  }

  return err;
}

static error_t
file_decode (struct store_enc *enc, const struct store_class *const *classes,
	     struct store **store)
{
  return store_std_leaf_decode (enc, _store_file_create, store);
}

static error_t
file_open (const char *name, int flags,
	   const struct store_class *const *classes,
	   struct store **store)
{
  return store_file_open (name, flags, store);
}

static error_t
fiopen (const char *name, file_t *file, int *mod_flags)
{
  if (*mod_flags & STORE_HARD_READONLY)
    *file = file_name_lookup (name, O_RDONLY, 0);
  else
    {
      *file = file_name_lookup (name, O_RDWR, 0);
      if (*file == MACH_PORT_NULL
	  && (errno == EACCES || errno == EROFS))
	{
	  *file = file_name_lookup (name, O_RDONLY, 0);
	  if (*file != MACH_PORT_NULL)
	    *mod_flags |= STORE_HARD_READONLY;
	}
      else if (*file != MACH_PORT_NULL)
	*mod_flags &= ~STORE_HARD_READONLY;
    }
  return *file == MACH_PORT_NULL ? errno : 0;
}

static void
ficlose (struct store *store)
{
  mach_port_deallocate (mach_task_self (), store->port);
  store->port = MACH_PORT_NULL;
}



static error_t
file_set_flags (struct store *store, int flags)
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
    ficlose (store);

  store->flags |= flags;	/* When inactive, anything goes.  */

  return 0;
}

static error_t
file_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    err = EINVAL;
  if (!err && (flags & STORE_INACTIVE))
    err = store->name
      ? fiopen (store->name, &store->port, &store->flags)
      : ENOENT;
  if (! err)
    store->flags &= ~flags;
  return err;
}

static error_t
file_map (const struct store *store, vm_prot_t prot, mach_port_t *memobj)
{
  error_t err;
  mach_port_t rd_memobj, wr_memobj;
  int ro = (store->flags & STORE_HARD_READONLY);

  if (store->num_runs != 1 || store->runs[0].start != 0)
    return EOPNOTSUPP;

  if ((prot & VM_PROT_WRITE) && ro)
    return EACCES;

  err = io_map (store->port, &rd_memobj, &wr_memobj);
  if (err)
    return err;

  *memobj = rd_memobj;

  if (ro && wr_memobj == MACH_PORT_NULL)
    return 0;
  else if (rd_memobj == wr_memobj)
    {
      if (rd_memobj != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), rd_memobj,
			    MACH_PORT_RIGHT_SEND, -1);
    }
  else
    {
      if (rd_memobj != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), rd_memobj);
      if (wr_memobj != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), wr_memobj);
      err = EOPNOTSUPP;
    }

  return err;
}

const struct store_class
store_file_class =
{
  STORAGE_HURD_FILE, "file", file_read, file_write, file_store_set_size,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, file_decode,
  file_set_flags, file_clear_flags, 0, 0, 0, file_open, 0, file_map
};
STORE_STD_CLASS (file);

static error_t
file_byte_read (struct store *store,
		store_offset_t addr, size_t index, size_t amount,
		void **buf, size_t *len)
{
  return io_read (store->port, (char **)buf, len, addr, amount);
}

static error_t
file_byte_write (struct store *store,
		 store_offset_t addr, size_t index,
		 const void *buf, size_t len,
		 size_t *amount)
{
  return io_write (store->port, (void *) buf, len, addr, amount);
}

struct store_class
store_file_byte_class =
{
  STORAGE_HURD_FILE, "file", file_byte_read, file_byte_write,
  file_store_set_size,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, file_decode,
  file_set_flags, file_clear_flags, 0, 0, 0, file_open, 0, file_map
};

/* Return a new store in STORE referring to the mach file FILE.  Consumes
   the send right FILE.  */
error_t
store_file_create (file_t file, int flags, struct store **store)
{
  struct store_run run;
  struct stat stat;
  error_t err = io_stat (file, &stat);

  if (err)
    return err;

  run.start = 0;
  run.length = stat.st_size;

  flags |= STORE_ENFORCED;	/* 'cause it's the whole file.  */

  return _store_file_create (file, flags, 1, &run, 1, store);
}

/* Like store_file_create, but doesn't query the file for information.  */
error_t
_store_file_create (file_t file, int flags, size_t block_size,
		    const struct store_run *runs, size_t num_runs,
		    struct store **store)
{
  if (block_size == 1)
    return _store_create (&store_file_byte_class,
			  file, flags, 1, runs, num_runs, 0, store);
  else
    return _store_create (&store_file_class,
			  file, flags, block_size, runs, num_runs, 0, store);
}

/* Open the file NAME, and return the corresponding store in STORE.  */
error_t
store_file_open (const char *name, int flags, struct store **store)
{
  file_t file;
  error_t err = fiopen (name, &file, &flags);
  if (! err)
    {
      err = store_file_create (file, flags, store);
      if (! err)
	{
	  err = store_set_name (*store, name);
	  if (err)
	    store_free (*store);
	}
      if (err)
	mach_port_deallocate (mach_task_self (), file);
    }
  return err;
}
