/* Parse run-time options

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

static const struct argp_option
std_runtime_options[] =
{
  {"remount", 'u',  0, 0, "Flush any meta-data cached in core"},
  {0, 0}
};

error_t
diskfs_set_options (int argc, char **argv)
{
  error_t err;
  int readonly = diskfs_readonly;
  int sync = diskfs_synchronous;
  int sync_interval = -1;
  int remount = 0;
  error_t parse_opt (int opt, char *arg, struct argp_state *argp)
    {
      switch (opt)
	{
	case 'r':
	  readonly = 1; break;
	case 'w':
	  readonly = 0; break;
	case 'u':
	  remount = 1; break;
	case 'n':
	  sync_interval = 0; sync = 0; break;
	case 's':
	  if (arg)
	    sync_interval = atoi (arg);
	  else
	    sync = 1;
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp common_argp = { diskfs_common_options, parse_opt };
  const struct argp *parents[] = { &common_argp, 0 };
  const struct argp argp = { std_runtime_options, parse_opt, 0, 0, parents };

  /* Call the user option parsing routine, giving it our set of options to do
     with as it pleases.  */
  err = diskfs_parse_runtime_options (argc, argv, &argp);
  if (err)
    return err;

  /* Do things in this order: remount, change readonly, change-sync; always
     do the remount while the disk is readonly, even if only temporarily.  */

  if (remount)
    {
      /* We can only remount while readonly.  */
      err = diskfs_set_readonly (1);
      if (!err)
	err = diskfs_remount ();
    }

  if (readonly != diskfs_readonly)
    if (err)
      diskfs_set_readonly (readonly); /* keep the old error.  */
    else
      err = diskfs_set_readonly (readonly);

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

  return err;
}
