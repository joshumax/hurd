/* `gcore' program for GNU Hurd: write a core dump from a running process.
   Copyright (C) 1992,2001,2002 Free Software Foundation, Inc.
   Written by Roland McGrath.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdio.h>
#include <stdlib.h>
#include <hurd.h>
#include <hurd/crash.h>
#include <hurd/paths.h>
#include <fcntl.h>
#include <argp.h>
#include <error.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (gcore);

static const struct argp argp =
{
  NULL, NULL,
  "PID...",
  "Generate a core dump file from a running process"
  "\vFor each PID, a core dump file \"core.PID\" is written."
};

int
main (int argc, char **argv)
{
  int status = 0;
  file_t crashserv;
  int argi;

  argp_parse (&argp, argc, argv, 0, &argi, 0);

  crashserv = file_name_lookup (_SERVERS_CRASH, 0, 0);
  if (crashserv == MACH_PORT_NULL)
    error (1, errno, "cannot reach crash server: %s", _SERVERS_CRASH);

  for (; argi < argc; ++argi)
    {
      char *end;
      pid_t pid;
      task_t task;

      pid = strtol (argv[argi], &end, 10);
      if (end == argv[argi] || *end != '\0')
	{
	  error (0, 0, "cannot parse process ID: %s", argv[argi]);
	  status = 1;
	  continue;
	}

      task = pid2task ((pid_t) pid);
      if (task == MACH_PORT_NULL)
	{
	  error (0, errno, "pid2task: %d", pid);
	  status = 1;
	}
      else
	{
	  file_t file;
	  char *name = 0;
	  asprintf (&name, "core.%d", pid);

	  file = file_name_lookup (name, O_WRONLY|O_CREAT,
					  0600 &~ getumask ());
	  if (file == MACH_PORT_NULL)
	    {
	      error (0, errno, "cannot create %s", name);
	      status = 1;
	    }
	  else
	    {
	      error_t err = crash_dump_task (crashserv, task, file,
					     0, 0, 0, 0, 0, 0,
					     MACH_PORT_NULL,
					     MACH_MSG_TYPE_COPY_SEND);
	      mach_port_deallocate (mach_task_self (), file);
	      if (err)
		{
		  (void) remove (name);
		  error (0, err, "crash_dump_task on %d to %s", pid, name);
		  status = 1;
		}
	      free (name);
	    }
	}
      mach_port_deallocate (mach_task_self (), task);
    }

  return status;
}
