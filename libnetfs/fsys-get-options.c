/* Unparse run-time options

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
netfs_S_fsys_get_options (fsys_t fsys,
			  char **data, mach_msg_type_number_t *data_len)
{
  error_t err;
  char *argz = 0;
  struct port_info *port =
    ports_lookup_port (netfs_port_bucket, fsys, netfs_control_class);

  if (!port)
    return EOPNOTSUPP;

#if NOT_YET
  rwlock_reader_lock (&netfs_fsys_lock);
#endif
  err = netfs_get_options (&argz, data_len);
#if NOT_YET
  rwlock_reader_unlock (&netfs_fsys_lock);
#endif

  if (!err && *data_len > 0)
    /* Move ARGZ from a malloced buffer into a vm_alloced one.  */
    {
      err =
	vm_allocate (mach_task_self (), (vm_address_t *)data, *data_len, 1);
      if (!err)
	bcopy (argz, *data, *data_len);
      free (argz);
    }

  ports_port_deref (port);

  return err;
}
