/* Get standard diskfs run-time options

   Copyright (C) 1995, 96,97,98,99,2002 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <stdio.h>
#include <argz.h>

#include "priv.h"

error_t
diskfs_append_std_options (char **argz, size_t *argz_len)
{
  error_t err;
  extern int diskfs_sync_interval;

  if (diskfs_readonly)
    err = argz_add (argz, argz_len, "--readonly");
  else
    err = argz_add (argz, argz_len, "--writable");

  if (!err && _diskfs_nosuid)
    err = argz_add (argz, argz_len, "--no-suid");
  if (!err && _diskfs_noexec)
    err = argz_add (argz, argz_len, "--no-exec");
  if (!err && _diskfs_noatime)
    err = argz_add (argz, argz_len, "--no-atime");
  if (!err && _diskfs_no_inherit_dir_group)
    err = argz_add (argz, argz_len, "--no-inherit-dir-group");

  if (! err)
    {
      if (diskfs_synchronous)
	err = argz_add (argz, argz_len, "--sync");
      else if (DEFAULT_SYNC_INTERVAL != diskfs_sync_interval)
	{
	  if (diskfs_sync_interval == 0)
	    err = argz_add (argz, argz_len, "--no-sync");
	  else
	    {
	      char buf[80];
	      sprintf (buf, "--sync=%d", diskfs_sync_interval);
	      err = argz_add (argz, argz_len, buf);
	    }
	}
    }

  return err;
}
