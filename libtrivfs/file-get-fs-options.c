/* Get runtime options given a file handle

   Copyright (C) 1996,98,2002 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <argz.h>
#include <hurd/fshelp.h>

#include "priv.h"
#include "trivfs_fs_S.h"

error_t
trivfs_S_file_get_fs_options (struct trivfs_protid *cred,
			      mach_port_t reply,
			      mach_msg_type_name_t reply_type,
			      data_t *data, mach_msg_type_number_t *len)
{
  error_t err;
  char *argz = 0;
  size_t argz_len = 0;

  if (! cred)
    return EOPNOTSUPP;

  err = argz_add (&argz, &argz_len, program_invocation_name);
  if (err)
    return err;

  err = trivfs_append_args (cred->po->cntl, &argz, &argz_len);
  if (! err)
    /* Put ARGZ into vm_alloced memory for the return trip.  */
    err = iohelp_return_malloced_buffer (argz, argz_len, data, len);
  else
    free (argz);

  return err;
}
