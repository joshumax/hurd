/* `gcore' for GNU Hurd.
   Copyright (C) 1992 Free Software Foundation
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
#include <hurd/core.h>
#include <limits.h>

int
main (int argc, char **argv)
{
  file_t coreserv;
  int i;

  if (argc < 2)
    {
    usage:
      fprintf (stderr, "Usage: %s PID ...\n", program_invocation_short_name);
      exit (1);
    }

  coreserv = path_lookup (_SERVERS_CORE, 0, 0);
  if (coreserv == MACH_PORT_NULL)
    {
      perror (_SERVERS_CORE);
      exit (1);
    }

  for (i = 1; i < argc; ++i)
    {
      char *end;
      pid_t pid;
      task_t task;

      pid = strtol (&argv[i], &end, 10);
      if (end == &argv[i] || *end != '\0')
	goto usage;

      task = pid2task ((pid_t) pid);
      if (task == MACH_PORT_NULL)
	fprintf (stderr, "pid2task: %d: %s\n", pid, strerror (errno));
      else
	{
	  char name[PATH_MAX];
	  file_t file;
	  sprintf (name, "core.%d", pid);
	  file = path_lookup (name, FS_LOOKUP_WRITE|FS_LOOKUP_CREATE,
			      0666 &~ getumask ());
	  if (file == MACH_PORT_NULL)
	    perror (name);
	  else
	    {
	      error_t err = core_dump_task (coreserv, task,
					    file,
					    0, 0,
					    getenv ("GNUTARGET"));
	      mach_port_deallocate (mach_task_self (), file);
	      if (err)
		{
		  (void) remove (name);
		  fprintf (stderr, "core_dump_task: %d: %s\n",
			   pid, strerror (err));
		}
	    }
	}
      mach_port_deallocate (mach_task_self (), task);
    }

  exit (0);
}
