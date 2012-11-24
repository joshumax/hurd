/* libdiskfs implementation of io.defs: io_pathconf
   Copyright (C) 1992, 1993, 1994, 1995, 1999 Free Software Foundation

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

#include <unistd.h>
#include "priv.h"
#include "io_S.h"
#include <dirent.h>
#include <limits.h>

/* Implement io_pathconf as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_pathconf (struct protid *cred,
		      int name,
		      int *value)
{
  if (!cred)
    return EOPNOTSUPP;

  switch (name)
    {
    case _PC_LINK_MAX:
      *value = diskfs_link_max;
      break;

    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
    case _PC_PIPE_BUF:
    case _PC_VDISABLE:
    case _PC_SOCK_MAXBUF:
    case _PC_PATH_MAX:
      *value = -1;
      break;

    case _PC_NAME_MAX:
      /* <hurd/hurd_types.defs> string_t constrains the upper bound.
	 The `struct dirent' format defined by libc further contrains it.  */
#define D_NAMLEN_MAX (UCHAR_MAX * sizeof (((struct dirent *) 0)->d_namlen))
      if (diskfs_name_max > D_NAMLEN_MAX || diskfs_name_max < 0)
	diskfs_name_max = D_NAMLEN_MAX;
      *value = diskfs_name_max;
      break;

    case _PC_NO_TRUNC:		/* enforced in diskfs_lookup */
      *value = 1; /* diskfs_name_max >= 0; */ /* see above */
      break;

    case _PC_CHOWN_RESTRICTED:
    case _PC_SYNC_IO:
    case _PC_ASYNC_IO:
      *value = 1;
      break;

    case _PC_PRIO_IO:
      *value = 0;
      break;

    case _PC_FILESIZEBITS:
      *value = 32;
      break;

    default:
      return EINVAL;
    }

  return 0;
}
