/*
   Copyright (C) 1994,2002 Free Software Foundation, Inc.

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
#include "trivfs_fs_S.h"

kern_return_t
trivfs_S_file_utimes (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      time_value_t atime, time_value_t mtime)
{
  return cred ? file_utimes (cred->realnode, atime, mtime) : EOPNOTSUPP;
}

kern_return_t
trivfs_S_file_utimens (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      struct timespec atime, struct timespec mtime)
{
  kern_return_t err;

  if (!cred)
    return EOPNOTSUPP;

#ifdef HAVE_FILE_UTIMENS
  err = file_utimens (cred->realnode, atime, mtime);

  if (err == EMIG_BAD_ID || err == EOPNOTSUPP)
#endif
    {
      time_value_t atim, mtim;

      if (atime.tv_nsec == UTIME_NOW)
        atim.microseconds = -1;
      else
        TIMESPEC_TO_TIME_VALUE (&atim, &atime);

      if (mtime.tv_nsec == UTIME_NOW)
        mtim.microseconds = -1;
      else
        TIMESPEC_TO_TIME_VALUE (&mtim, &mtime);

      err = file_utimes (cred->realnode, atim, mtim);
    }

  return err;
}
