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

#include <stdio.h>
#include <argz.h>

#include "priv.h"

error_t
diskfs_append_std_options (char **argz, unsigned *argz_len)
{
  error_t err;
  extern int diskfs_sync_interval;

  if (diskfs_readonly)
    err = argz_add (argz, argz_len, "--readonly");
  else
    err = argz_add (argz, argz_len, "--writable");
  if (err)
    return err;

  if (diskfs_synchronous)
    err = argz_add (argz, argz_len, "--sync");
  else if (diskfs_sync_interval == 0)
    err = argz_add (argz, argz_len, "--nosync");
  else
    {
      char buf[80];
      sprintf (buf, "--sync=%d", diskfs_sync_interval);
      err = argz_add (argz, argz_len, buf);
    }
  if (err)
    free (argz);		/* Free the first option allocated. */

  return err;
}
