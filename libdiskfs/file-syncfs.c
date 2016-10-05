/* libdiskfs implementation of fs.defs: file_syncfs
   Copyright (C) 1992, 1993, 1994, 1995, 1996 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"
#include "fs_S.h"
#include <hurd/fsys.h>

struct args
{
  int wait;
};

static error_t
helper (void *cookie, const char *name, mach_port_t control)
{
  struct args *args = cookie;
  (void) name;
  fsys_syncfs (control, args->wait, 1);
  return 0;
}

/* Implement file_syncfs as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_syncfs (struct protid *cred,
		      int wait,
		      int dochildren)
{
  struct args args = { wait };

  if (!cred)
    return EOPNOTSUPP;

  if (dochildren)
    fshelp_map_active_translators (helper, &args);

  if (diskfs_synchronous)
    wait = 1;

  if (! diskfs_readonly)
    {
      diskfs_sync_everything (wait);
      diskfs_set_hypermetadata (wait, 0);
    }

  return 0;
}
