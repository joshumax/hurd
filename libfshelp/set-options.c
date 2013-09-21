/* Standard filesystem runtime option parsing

   Copyright (C) 1996, 1998, 1999 Free Software Foundation, Inc.

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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA. */

#include <mach.h>
#include <argp.h>
#include <argz.h>
#include <alloca.h>

#include "fshelp.h"

/* XXX this is not currently so useful, but the new fsys_set_options will
   have more commonly used code that can be put here.  */

/* Invoke ARGP with data from DATA & LEN, in the standard way.  */
error_t
fshelp_set_options (const struct argp *argp, int flags,
		    const char *argz, size_t argz_len, void *input)
{
  int argc = argz_count (argz, argz_len);
  char **argv = alloca (sizeof (char *) * (argc + 1));

  argz_extract ((char *) argz, argz_len, argv);

  return
    argp_parse (argp, argc, argv,
		flags | ARGP_NO_ERRS | ARGP_NO_HELP | ARGP_PARSE_ARGV0,
		0, input);
}
