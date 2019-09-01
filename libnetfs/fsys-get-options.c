/* Unparse run-time options

   Copyright (C) 1995, 1996, 1998, 2001 Free Software Foundation, Inc.

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
#include <argz.h>
#include <hurd/fsys.h>
#include <string.h>

#include "netfs.h"
#include "fsys_S.h"

/* This code is originally from libdiskfs; things surrounded by `#if NOT_YET'
   are pending libnetfs being fleshed out some more.  */

/* Implement fsys_get_options as described in <hurd/fsys.defs>. */
error_t
netfs_S_fsys_get_options (struct netfs_control *port,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_type,
			  data_t *data, mach_msg_type_number_t *data_len)
{
  error_t err;
  char *argz = 0;
  size_t argz_len = 0;

  if (!port)
    return EOPNOTSUPP;

  err = argz_add (&argz, &argz_len, program_invocation_name);
  if (! err)
    {
#if NOT_YET
      pthread_rwlock_rdlock (&netfs_fsys_lock);
#endif
      err = netfs_append_args (&argz, &argz_len);
#if NOT_YET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
    }

  if (! err)
    /* Move ARGZ from a malloced buffer into a vm_alloced one.  */
    err = iohelp_return_malloced_buffer (argz, argz_len, data, data_len);
  else
    free (argz);

  return err;
}
