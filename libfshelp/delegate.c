/* fshelp_delegate_translation

   Copyright (C) 1995,96,99,2000,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#include <errno.h>
#include <string.h>
#include <alloca.h>
#include <stdlib.h>
#include <argz.h>
#include <hurd.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>

/* Try to hand off responsibility from a translator to the server located on
   the node SERVER_NAME.  REQUESTOR is the translator's bootstrap port, and
   ARGV is the command line.  If SERVER_NAME is NULL, then a name is
   concocted by appending ARGV[0] to _SERVERS.  */
error_t
fshelp_delegate_translation (const char *server_name,
			     mach_port_t requestor, char **argv)
{
  error_t err;
  file_t server;

  if (! server_name)
    {
      char *buf = alloca (strlen (argv[0]) + sizeof (_SERVERS));
      strcpy (buf, _SERVERS);
      strcat (buf, argv[0]);
      server_name = buf;
    }

  server = file_name_lookup (server_name, 0, 0);
  if (server != MACH_PORT_NULL)
    {
      char *argz;
      size_t argz_len;
      err = argz_create (argv, &argz, &argz_len);
      if (!err)
	{
	  err = fsys_forward (server,
			      requestor, MACH_MSG_TYPE_COPY_SEND,
			      argz, argz_len);
	  free (argz);
	}
    }
  else
    err = errno;

  return err;
}
