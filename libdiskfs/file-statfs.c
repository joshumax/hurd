/* libdiskfs implementation of fs.defs: file_statfs
   Copyright (C) 1992, 1993, 1994, 1998 Free Software Foundation

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

#include <sys/statvfs.h>

#include "priv.h"
#include "fs_S.h"

/* Implement file_getcontrol as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_statfs (struct protid *file,
		      fsys_statfsbuf_t *statbuf)
{
  if (!file)
    return EOPNOTSUPP;

  statbuf->f_flag = 0;
  if (diskfs_readonly)
    statbuf->f_flag |= ST_RDONLY;
  if (_diskfs_nosuid)
    statbuf->f_flag |= ST_NOSUID;
  if (_diskfs_noexec)
    statbuf->f_flag |= ST_NOEXEC;
  if (diskfs_synchronous)
    statbuf->f_flag |= ST_SYNCHRONOUS;

  diskfs_set_statfs (statbuf);

  return 0;
}
