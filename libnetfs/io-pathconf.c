/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <unistd.h>
#include "netfs.h"
#include "io_S.h"


error_t
netfs_S_io_pathconf (struct protid *user,
		     int name,
		     int *value)
{
  if (!user)
    return EOPNOTSUPP;

  switch (name)
    {
    case _PC_LINK_MAX:
    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
    case _PC_PIPE_BUF:
    case _PC_VDISABLE:
    case _PC_SOCK_MAXBUF:
    case _PC_PATH_MAX:
      *value = -1;
      break;
      
    case _PC_NAME_MAX:
      *value = 1024;		/* see <hurd/hurd_types.defs> string_t */
      break;

    case _PC_CHOWN_RESTRICTED:
    case _PC_NO_TRUNC:		/* look at string_t trunc behavior in MiG */
      *value = 1;
      break;
      
    case _PC_PRIO_IO:
    case _PC_SYNC_IO:
    case _PC_ASYNC_IO:
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
