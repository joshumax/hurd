/* Get standard diskfs run-time options

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "priv.h"

error_t
diskfs_append_std_options (char **argz, unsigned *argz_len)
{
  extern int diskfs_sync_interval;

  void append_opt (char *str)
    {
      unsigned old_end = *argz_len;
      *argz_len += strlen (str) + 1;
      *argz = realloc (*argz, *argz_len);
      strcpy (*argz + old_end, str);;
    }

  if (diskfs_readonly)
    append_opt ("--readonly");
  else
    append_opt ("--writable");

  if (diskfs_synchronous)
    append_opt ("--sync");
  else if (diskfs_sync_interval == 0)
    append_opt ("--nosync");
  else
    {
      char buf[80];
      sprintf (buf, "--sync=%d", diskfs_sync_interval);
      append_opt (buf);
    }

  return 0;
}
