/* Parse run-time options

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#define SHORT_OPTIONS "rwsnm"

static struct option long_options[] =
{
  {"readonly", no_argument, 0, 'r'},
  {"writable", no_argument, 0, 'w'},
  {"sync", optional_argument, 0, 's'},
  {"nosync", no_argument, 0, 'n'},
  {"remount", no_argument, 0, 'm'},
  {0, 0, 0, 0}
};

error_t
diskfs_set_options (int argc, char **argv)
{
  int readonly = diskfs_readonly;
  int sync = diskfs_synchronous;
  int sync_interval = -1;
  error_t parse_opt (int opt, char *arg)
    {
      switch (opt)
	{
	case 'r':
	  readonly = 1; break;
	case 'w':
	  readonly = 0; break;
	case 'n':
	  sync_interval = 0; sync = 0; break;
	case 's':
	  if (optarg)
	    sync_interval = atoi (arg);
	  else
	    sync = 1;
	default:
	  return EINVAL;
	}
      return 0;
    }
  struct options options = { SHORT_OPTIONS, long_options, parse_opt, 0 };

  /* Call the user option parsing routine, giving it our set of options to do
     with as it pleases.  */
  error_t err = diskfs_parse_runtime_options (argc, argv, &options);

  if (err)
    return err;

  /* Do things in this order: change-read/write, remount, change-sync. */

  /* Going writable seems easy, but how do we switch to readonly mode?  There
     might be thread that are past the initial readonly checks which would
     fail a !readonly assertion if we just set the variable...  */
  if (!readonly)
    diskfs_readonly = 0;

  /* Remount...  */

  /* Change sync mode.  */
  if (sync)
    {
      diskfs_synchronous = 1;
      diskfs_set_sync_interval (0); /* Don't waste time syncing.  */
    }
  else
    {
      diskfs_synchronous = 0;
      if (sync_interval >= 0)
	diskfs_set_sync_interval (sync_interval);
    }

  return 0;
}
