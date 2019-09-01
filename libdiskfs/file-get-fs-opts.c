/* Get run-time file system options

   Copyright (C) 1995,96,98,2002 Free Software Foundation, Inc.
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <errno.h>
#include <string.h>
#include <argz.h>
#include "priv.h"
#include "fs_S.h"

error_t
diskfs_S_file_get_fs_options (struct protid *cred,
			      data_t *data, size_t *data_len)
{
  error_t err;
  char *argz = 0;
  size_t argz_len = 0;

  if (! cred)
    return EOPNOTSUPP;

  err = argz_add (&argz, &argz_len, program_invocation_name);
  if (err)
    return err;

  pthread_rwlock_rdlock (&diskfs_fsys_lock);
  err = diskfs_append_args (&argz, &argz_len);
  pthread_rwlock_unlock (&diskfs_fsys_lock);

  if (! err)
    /* Move ARGZ from a malloced buffer into a vm_alloced one.  */
    err = iohelp_return_malloced_buffer (argz, argz_len, data, data_len);
  else
    free (argz);

  return err;
}
