/* Default hook for argp --version handling

   Copyright (C) 1996 Free Software Foundation, Inc.

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
#include <version.h>

static void
_print_version (FILE *stream, struct argp_state *state)
{
  if (argp_program_version)
    /* If this is non-zero, then the program's probably defined it, so let
       that take precedence over the default.  */
    fputs (argp_program_version, stream);
  else
    {
      fprintf (stream, "%s %s ", diskfs_server_name, diskfs_server_version);
      if (diskfs_extra_version[0])
	fprintf (stream, "(%s) ", diskfs_extra_version);
    }
  /* And because diskfs is big and huge, put our information out too. */
  fputs (STANDARD_HURD_VERSION (libdiskfs), stream);
  
  putc ('\n', stream);
}

void (*argp_program_version_hook) (FILE *stream, struct argp_state *state)
     = _print_version;
