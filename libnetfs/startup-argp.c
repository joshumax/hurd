/* Standard startup-time command line parser

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
#include <argp.h>

#include "netfs.h"

static const struct argp_option
startup_options[] =
{
  {0}
};

static error_t
parse_startup_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp
startup_argp =
  { startup_options, parse_startup_opt };

const struct argp *netfs_startup_argp = &startup_argp;
