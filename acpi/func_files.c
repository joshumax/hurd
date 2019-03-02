/*
   Copyright (C) 2017 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/*
 * Per-function files implementation.
 *
 * Implementation of all files repeated for each function.
 */

#include <func_files.h>
#include <assert.h>

/* Read an acpi table */
error_t
io_acpi_table (struct acpi_table *t, off_t offset, size_t *len, void *data)
{
  error_t err;
  size_t datalen;

  /* This should never happen */
  assert_backtrace (t != 0);

  datalen = t->datalen;

  /* Don't exceed the size of the acpi table */
  if (offset > datalen)
    return EINVAL;
  if ((offset + *len) > datalen)
    *len = datalen - offset;

  memcpy (data, t->data + offset, *len);

  return err;
}

/* Read from an acpi table file */
error_t
io_acpi_file (struct acpifs_dirent *e, off_t offset, size_t *len,
              void *data)
{
  size_t datalen;
  struct acpi_table *table;

  /* This should never happen */
  assert_backtrace (e->acpitable != 0);

  /* Get the table */
  table = e->acpitable;

  datalen = table->datalen;
  /* Don't exceed the region size */
  if (offset > datalen)
    return EINVAL;
  if ((offset + *len) > datalen)
    *len = datalen - offset;

  memcpy (data, table->data + offset, *len);

  return 0;
}
