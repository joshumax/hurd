/* Copy store backend

   Copyright (C) 1995,96,97,99,2000,01,02 Free Software Foundation, Inc.
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
#include <malloc.h>
#include <sys/mman.h>
#include <mach.h>

#define page_aligned(addr) (((size_t) addr & (vm_page_size - 1)) == 0)

#include "store.h"

static error_t
copy_read (struct store *store, store_offset_t addr, size_t index,
	   size_t amount, void **buf, size_t *len)
{
  char *data = store->hook + (addr * store->block_size);

  if (page_aligned (data) && page_aligned (amount))
    /* When reading whole pages, we can avoid any real copying.  */
    return vm_read (mach_task_self (),
		    (vm_address_t) data, amount,
		    (pointer_t *) buf, len);

  if (*len < amount)
    /* Have to allocate memory for the return value.  */
    {
      *buf = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*buf == MAP_FAILED)
	return errno;
    }

  memcpy (*buf, data, amount);
  *len = amount;
  return 0;
}

static error_t
copy_write (struct store *store,
	    store_offset_t addr, size_t index,
	    const void *buf, size_t len, size_t *amount)
{
  char *data = store->hook + (addr * store->block_size);

  if (page_aligned (data) && page_aligned (len) && page_aligned (buf))
    {
      /* When writing whole pages, we can avoid any real copying.  */
      error_t err = vm_write (mach_task_self (),
			      (vm_address_t) data, (vm_address_t) buf, len);
      *amount = len;
      return err;
    }

  memcpy (data, buf, len);
  *amount = len;
  return 0;
}

static error_t
copy_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
}

error_t
copy_allocate_encoding (const struct store *store, struct store_enc *enc)
{
  return EOPNOTSUPP;
}

error_t
copy_encode (const struct store *store, struct store_enc *enc)
{
  return EOPNOTSUPP;
}

static error_t
copy_decode (struct store_enc *enc, const struct store_class *const *classes,
	     struct store **store)
{
  return EOPNOTSUPP;
}

static error_t
copy_open (const char *name, int flags,
	   const struct store_class *const *classes,
	   struct store **store)
{
  return store_copy_open (name, flags, classes, store);
}

static error_t
copy_set_flags (struct store *store, int flags)
{
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    /* Trying to set flags we don't support.  */
    return EINVAL;

  /* ... */

  store->flags |= flags;	/* When inactive, anything goes.  */

  return 0;
}

static error_t
copy_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  if ((flags & ~(STORE_INACTIVE | STORE_ENFORCED)) != 0)
    err = EINVAL;
  /* ... */
  if (! err)
    store->flags &= ~flags;
  return err;
}

/* Called just before deallocating STORE.  */
void
copy_cleanup (struct store *store)
{
  if (store->size > 0)
    munmap (store->hook, store->size);
}

/* Copy any format-dependent fields in FROM to TO; if there's some reason
   why the copy can't be made, an error should be returned.  This call is
   made after all format-indendependent fields have been cloned.  */
error_t
copy_clone (const struct store *from, struct store *to)
{
  void *buf;
  buf = mmap (0, to->size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  if (buf != (void *) -1)
    {
      to->hook = buf;
      memcpy (to->hook, from->hook, from->size);
      return 0;
    }
  return errno;
}

const struct store_class
store_copy_class =
{
  STORAGE_COPY, "copy", copy_read, copy_write, copy_set_size,
  copy_allocate_encoding, copy_encode, copy_decode,
  copy_set_flags, copy_clear_flags, copy_cleanup, copy_clone, 0, copy_open
};
STORE_STD_CLASS (copy);

/* Return a new store in STORE which contains a snapshot of the contents of
   the store FROM; FROM is consumed.  */
error_t
store_copy_create (struct store *from, int flags, struct store **store)
{
  error_t err;
  struct store_run run;

  run.start = 0;
  run.length = from->size;

  flags |= STORE_ENFORCED;	/* Only uses local resources.  */

  err =
    _store_create (&store_copy_class,
		   MACH_PORT_NULL, flags, from->block_size, &run, 1, 0,
		   store);
  if (! err)
    {
      size_t buf_len = 0;

      /* Copy the input store.  */
      err = store_read (from, 0, from->size, &(*store)->hook, &buf_len);

      if (! err)
	/* Set the store name.  */
	{
	  if (from->name)
	    {
	      size_t len =
		strlen (from->class->name) + 1 + strlen (from->name) + 1;
	      (*store)->name = malloc (len);
	      if ((*store)->name)
		snprintf ((*store)->name, len,
			  "%s:%s", from->class->name, from->name);
	    }
	  else
	    (*store)->name = strdup (from->class->name);

	  if (! (*store)->name)
	    err = ENOMEM;
	}

      if (err)
	store_free (*store);
    }

  return err;
}

/* Return a new store in STORE which contains the memory buffer BUF, of
   length BUF_LEN, and uses the block size BLOCK_SIZE.  BUF must be
   vm_allocated, and will be consumed, and BUF_LEN must be a multiple of
   BLOCK_SIZE.  */
error_t
store_buffer_create (void *buf, size_t buf_len, int flags,
		     struct store **store)
{
  error_t err;
  struct store_run run;

  run.start = 0;
  run.length = buf_len;

  flags |= STORE_ENFORCED;	/* Only uses local resources.  */

  err =
    _store_create (&store_copy_class,
		   MACH_PORT_NULL, flags, 1, &run, 1, 0, store);
  if (! err)
    (*store)->hook = buf;

  return err;
}

/* Open the copy store NAME -- which consists of another store-class name, a
   ':', and a name for that store class to open -- and return the
   corresponding store in STORE.  CLASSES is used to select classes specified
   by the type name; if it is 0, STORE_STD_CLASSES is used.  */
error_t
store_copy_open (const char *name, int flags,
		 const struct store_class *const *classes,
		 struct store **store)
{
  struct store *from;
  error_t err =
    store_typed_open (name, flags | STORE_HARD_READONLY, classes, &from);

  if (! err)
    {
      err = store_copy_create (from, flags, store);
      if (err)
	store_free (from);
    }

  return err;
}
