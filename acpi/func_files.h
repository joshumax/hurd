/*
   Copyright (C) 2018 Free Software Foundation, Inc.

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

/* Per-function files header */

#ifndef FUNC_FILES_H
#define FUNC_FILES_H

#include <acpifs.h>
#include <acpi.h>

typedef int (*acpi_read_op_t) (struct acpi_table *t, void *data,
                               off_t offset, size_t *len);

/* Tables */
#define DIR_TABLES_NAME	"tables"

error_t io_read_table (struct acpi_table *t, struct acpifs_dirent *e,
                       off_t offset, size_t *len, void *data);
error_t io_acpi_file (struct acpifs_dirent *e, off_t offset, size_t *len,
                      void *data);

#endif /* FUNC_FILES_H */
