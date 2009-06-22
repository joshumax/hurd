/* Support for mach's mapped time

   Copyright (C) 1996, 1997, 2000, 2007, 2009 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#ifndef __MAPTIME_H__
#define __MAPTIME_H__

#include <mach/time_value.h>
#include <sys/time.h>
#include <errno.h>

/* Return the mach mapped time page in MTIME.  If USE_MACH_DEV is false, then
   the hurd time device DEV_NAME, or "/dev/time" if DEV_NAME is 0, is
   used.  If USE_MACH_DEV is true, the mach device DEV_NAME, or "time" if
   DEV_NAME is 0, is used; this is a privileged operation.  The mapped time
   may be converted to a struct timeval at any time using maptime_read.  */
error_t maptime_map (int use_mach_dev, char *dev_name,
		     volatile struct mapped_time_value **mtime);

/* Read the current time from MTIME into TV.  This should be very fast.  */
void maptime_read (volatile struct mapped_time_value *mtime, struct timeval *tv);

/* Inlining optimizations.  */

#include <features.h>

#ifdef __USE_EXTERN_INLINES
# ifndef MAPTIME_H_EXTERN_INLINE
#  define MAPTIME_H_EXTERN_INLINE __extern_inline
# endif

MAPTIME_H_EXTERN_INLINE void
maptime_read (volatile struct mapped_time_value *mtime, struct timeval *tv)
{
  do
    {
      tv->tv_sec = mtime->seconds;
      tv->tv_usec = mtime->microseconds;
    }
  while (tv->tv_sec != mtime->check_seconds);
}

#endif /* __USE_EXTERN_INLINES */

#endif /* __MAPTIME_H__ */
