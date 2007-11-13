/* 
   Copyright (C) 1999, 2007 Free Software Foundation, Inc.

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

#include "fshelp.h"

/* Change the stat times of NODE as indicated by WHAT (from the set TOUCH_*)
   to the current time.  */
void
fshelp_touch (struct stat *st, unsigned what,
	      volatile struct mapped_time_value *maptime)
{
  struct timeval tv;

  maptime_read (maptime, &tv);

  if (what & TOUCH_ATIME)
    {
      st->st_atim.tv_sec = tv.tv_sec;
      st->st_atim.tv_nsec = tv.tv_usec * 1000;
    }
  if (what & TOUCH_CTIME)
    {
      st->st_ctim.tv_sec = tv.tv_sec;
      st->st_ctim.tv_nsec = tv.tv_usec * 1000;
    }
  if (what & TOUCH_MTIME)
    {
      st->st_mtim.tv_sec = tv.tv_sec;
      st->st_mtim.tv_nsec = tv.tv_usec * 1000;
    }
}
