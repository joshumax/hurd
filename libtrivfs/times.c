/* 
   Copyright (C) 1994, 1999, 2007 Free Software Foundation

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

error_t
trivfs_set_atime (struct trivfs_control *cntl)
{
  error_t err;
#ifdef HAVE_FILE_UTIMENS
  struct timespec atime;
  struct timespec mtime;

  atime.tv_sec = 0;
  atime.tv_nsec = UTIME_NOW;
  mtime.tv_sec = 0;
  mtime.tv_nsec = UTIME_OMIT;

  err = file_utimens (cntl->underlying, atime, mtime);

  if (err == MIG_BAD_ID || err == EOPNOTSUPP)
#endif
    {
      struct stat st;
      time_value_t atim, mtim;

      io_stat (cntl->underlying, &st);

      TIMESPEC_TO_TIME_VALUE (&atim, &st.st_atim);
      mtim.microseconds = -1;
      err = file_utimes (cntl->underlying, atim, mtim);
    }

  return err;
}

error_t
trivfs_set_mtime (struct trivfs_control *cntl)
{
  error_t err;
#ifdef HAVE_FILE_UTIMENS
  struct timespec atime;
  struct timespec mtime;

  atime.tv_sec = 0;
  atime.tv_nsec = UTIME_OMIT;
  mtime.tv_sec = 0;
  mtime.tv_nsec = UTIME_NOW;

  err = file_utimens (cntl->underlying, atime, mtime);

  if (err == MIG_BAD_ID || err == EOPNOTSUPP)
#endif
    {
      struct stat st;
      time_value_t atim, mtim;

      io_stat (cntl->underlying, &st);

      atim.microseconds = -1;
      TIMESPEC_TO_TIME_VALUE (&mtim, &st.st_mtim);
      err = file_utimes (cntl->underlying, atim, mtim);
    }

  return err;
}
