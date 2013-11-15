/* Some handy utility routines for fsys control ports

   Copyright (C) 1996, 1999 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <errno.h>
#include <argz.h>
#include <mach.h>
#include <sys/mman.h>
#include <hurd/fsys.h>
#include <string.h>

/* Make FSYS readonly or writable.  */
error_t
fsys_set_readonly (fsys_t fsys, int readonly)
{
  error_t err;
  char *opts = readonly ? "--readonly" : "--writable";
  size_t opts_len = strlen (opts) + 1;
  err = fsys_set_options (fsys, opts, opts_len, 0);
  if (err == EINVAL)
    err = EOPNOTSUPP;
  return err;
}

/* Ask FSYS whether it's readonly, returning the result in READONLY; we don't
   really have a good method for this, other than asking for it's options and
   looking for `--readonly' or `--writable'.  If we see neither, return
   EOPNOTSUPP.  */
error_t
fsys_get_readonly (fsys_t fsys, int *readonly)
{
  error_t err;
  char _opts[200], *opts = _opts;
  size_t opts_len = sizeof opts;

  err = fsys_get_options (fsys, &opts, &opts_len);
  if (! err)
    {
      char *opt;
      int ok = 0;

      for (opt = opts
	   ; !ok && opt && opt < opts + opts_len
	   ; opt = argz_next (opts, opts_len, opt))
	if (strcasecmp (opt, "--readonly") == 0)
	  {
	    *readonly = 1;
	    ok = 1;
	  }
	else if (strcasecmp (opt, "--writable") == 0)
	  {
	    *readonly = 0;
	    ok = 1;
	  }

      if (! ok)
	err = EOPNOTSUPP; /* So far as we know...  */

      if (opts != _opts)
	/* Free out-of-line memory returned by fsys_get_options.  */
	munmap (opts, opts_len);
    }

  return err;
}

/* Tell FSYS to remount itself.  */
error_t
fsys_update (fsys_t fsys, int readonly)
{
  error_t err;
  char *opts = "--update";
  size_t opts_len = strlen (opts) + 1;
  err = fsys_set_options (fsys, opts, opts_len, 0);
  if (err == EINVAL)
    err = EOPNOTSUPP;
  return err;
}
