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

static struct argp_option
std_runtime_options[] =
{
  {"readonly", 'r', 0, 0, "Never write to disk or allow opens for writing"},
  {"rdonly",   0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"writable", 'w', 0, 0, "Use normal read/write behavior"},
  {"rdwr",     0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"sync",     's', "INTERVAL", OPTION_ARG_OPTIONAL,
     "If INTERVAL is supplied, sync all data not actually written to disk"
     " every INTERVAL seconds, otherwise operate in synchronous mode (the"
     " default is to sync every 30 seconds)"},
  {"nosync",  'n',  0, 0, "Don't automatically sync data to disk"},
  {"remount", 'u',  0, 0, "Flush any meta-data cached in core"},
  {0, 0}
};

error_t
diskfs_set_options (int argc, char **argv)
{
  int readonly = diskfs_readonly;
  int sync = diskfs_synchronous;
  int sync_interval = -1;
  error_t parse_opt (int opt, char *arg, struct argp_state *argp)
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
	  if (arg)
	    sync_interval = atoi (arg);
	  else
	    sync = 1;
	  break;
	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = { std_runtime_options, parse_opt };

  /* Call the user option parsing routine, giving it our set of options to do
     with as it pleases.  */
  error_t err = diskfs_parse_runtime_options (argc, argv, &argp);

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
