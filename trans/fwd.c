/* A translator to start up a central translation server

   Note: most translators that use a central server will look for the server
   and forward the request to the server if they find one, otherwise doing
   the translation themselves.

   Copyright (C) 1995, 1998, 1999 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <error.h>
#include <stdio.h>
#include <hurd/fshelp.h>

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  if (argc < 2 || *argv[1] == '-')
    {
      fprintf (stderr, "Usage: %s SERVER [TRANS_NAME [TRANS_ARG...]]\n",
	       program_invocation_name);
      return 1;
    }

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "must be started as a translator");

  err = fshelp_delegate_translation (argv[1], bootstrap, argv + 2);
  if (err)
    error (3, err, "%s", argv[1]);

  return 0;
}
