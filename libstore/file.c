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

static struct store_meths
file_meths = {file_read, file_write};

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

static struct store_meths
file_byte_meths = {file_byte_read, file_byte_write};

/* Return a new store in STORE referring to the mach file FILE.  Consumes
   the send right FILE.  */
error_t
store_file_create (file_t file, struct store **store)
{
  off_t runs[2];
  struct stat stat;
  error_t err = io_stat (file, &stat);

  if (err)
    return err;

  runs[0] = 0;
  runs[1] = stat.st_size;

  return _store_file_create (file, 1, runs, 2, store);
}

/* Like store_file_create, but doesn't query the file for information.  */
error_t
_store_file_create (file_t file, size_t block_size,
		    const off_t *runs, size_t runs_len,
		    struct store **store)
{
  if (block_size == 1)
    *store = _make_store (STORAGE_HURD_FILE, &file_byte_meths, file, 1,
			  runs, runs_len, 0);
  else if ((block_size & (block_size - 1)) == 0)
    *store = _make_store (STORAGE_HURD_FILE, &file_meths, file, block_size,
			  runs, runs_len, 0);
  else
    return EINVAL;		/* block size not a power of two */
  return *store ? 0 : ENOMEM;
}
