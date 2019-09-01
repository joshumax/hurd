/* Parse run-time options

   Copyright (C) 1995, 1996, 2001 Free Software Foundation, Inc.

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

struct args
{
  char *data;
  mach_msg_type_number_t len;
  int do_children;
};

static error_t
helper (void *cookie, const char *name, mach_port_t control)
{
  struct args *args = cookie;
  error_t err;
  (void) name;
  err = fsys_set_options (control, args->data, args->len, args->do_children);
  if (err == MIG_SERVER_DIED || err == MACH_SEND_INVALID_DEST)
    err = 0;
  return err;
}

/* Implement fsys_set_options as described in <hurd/fsys.defs>. */
error_t
netfs_S_fsys_set_options (struct netfs_control *pt,
			  mach_port_t reply,
			  mach_msg_type_name_t reply_type,
			  data_t data, mach_msg_type_number_t data_len,
			  int do_children)
{
  error_t err = 0;
  struct args args = { data, data_len, do_children };

  if (!pt)
    return EOPNOTSUPP;

  if (do_children)
    {
#if NOT_YET
      pthread_rwlock_wrlock (&netfs_fsys_lock);
#endif
      err = fshelp_map_active_translators (helper, &args);
#if NOT_YET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
    }

  if (!err)
    {
#if NOT_YET
      pthread_rwlock_wrlock (&netfs_fsys_lock);
#endif
      err = netfs_set_options (data, data_len);
#if NOT_YET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
    }

  return err;
}
