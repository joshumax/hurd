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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <hurd.h>

#include <hurd/io.h>

#include "store.h"

static error_t
file_read (struct store *store,
	   off_t addr, size_t index, mach_msg_type_number_t amount,
	   char **buf, mach_msg_type_number_t *len)
{
  size_t bsize = store->block_size;
  error_t err = io_read (store->port, buf, len, addr * bsize, amount);
  char rep_buf[20];
  if (err)
    strcpy (rep_buf, "-");
  else if (*len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), *buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), *buf);
  fprintf (stderr, "; file_read (%ld, %d, %d) [%d] => %s, %s, %d\n",
	   addr, index, amount, store->block_size, err ? strerror (err) : "-",
	   rep_buf, err ? 0 : *len);
  return err;
}

static error_t
file_write (struct store *store,
	   off_t addr, size_t index, char *buf, mach_msg_type_number_t len,
	   mach_msg_type_number_t *amount)
{
  size_t bsize = store->block_size;
  char rep_buf[20];
  if (len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), buf);
  fprintf (stderr, "; file_write (%ld, %d, %s, %d)\n",
	   addr, index, rep_buf, len);
  return io_write (store->port, buf, len, addr * bsize, amount);
}

static error_t
file_decode (struct store_enc *enc, struct store_class *classes,
	     struct store **store)
{
  return store_std_leaf_decode (enc, _store_file_create, store);
}

static struct store_class
file_class =
{
  STORAGE_HURD_FILE, "file", file_read, file_write,
  store_std_leaf_allocate_encoding, store_std_leaf_encode, file_decode
};
_STORE_STD_CLASS (file_class);

static error_t
file_byte_read (struct store *store,
		off_t addr, size_t index, mach_msg_type_number_t amount,
		char **buf, mach_msg_type_number_t *len)
{
  error_t err = io_read (store->port, buf, len, addr, amount);
  char rep_buf[20];
  if (err)
    strcpy (rep_buf, "-");
  else if (*len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), *buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), *buf);
  fprintf (stderr, "; file_byte_read (%ld, %d, %d) => %s, %s, %d\n",
	   addr, index, amount, err ? strerror (err) : "-",
	   rep_buf, err ? 0 : *len);
  return err;
}

static error_t
file_byte_write (struct store *store,
		 off_t addr, size_t index,
		 char *buf, mach_msg_type_number_t len,
		 mach_msg_type_number_t *amount)
{
  char rep_buf[20];
  if (len > sizeof rep_buf - 3)
    sprintf (rep_buf, "\"%.*s\"...", (int)(sizeof rep_buf - 6), buf);
  else
    sprintf (rep_buf, "\"%.*s\"", (int)(sizeof rep_buf - 3), buf);
  fprintf (stderr, "; file_byte_write (%ld, %d, %s, %d)\n",
	   addr, index, rep_buf, len);
  return io_write (store->port, buf, len, addr, amount);
}

static struct store_class
file_byte_class = {STORAGE_HURD_FILE, "file", file_byte_read, file_byte_write};

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
    *store = _make_store (&file_byte_class, file, flags, 1, runs, num_runs, 0);
  else if ((block_size & (block_size - 1)) == 0)
    *store =
      _make_store (&file_class, file, flags, block_size, runs, num_runs, 0);
  else
    return EINVAL;		/* block size not a power of two */
  return *store ? 0 : ENOMEM;
}

/* Open the file NAME, and return the corresponding store in STORE.  */
error_t
store_file_open (const char *name, int flags, struct store **store)
{
  error_t err;
  int open_flags = (flags & STORE_HARD_READONLY) ? O_RDONLY : O_RDWR;
  file_t node = file_name_lookup (name, open_flags, 0);

  if (node == MACH_PORT_NULL)
    return errno;

  err = store_file_create (node, flags, store);
  if (err)
    mach_port_deallocate (mach_task_self (), node);

  return err;
}
