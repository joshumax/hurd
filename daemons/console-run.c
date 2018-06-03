/* Run a program on the console, trying hard to get the console open.
   Copyright (C) 1999, 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <paths.h>
#include <hurd.h>
#include <hurd/fshelp.h>
#include <device/device.h>

static mach_port_t
get_console ()
{
  mach_port_t device_master, console;
  error_t err = get_privileged_ports (0, &device_master);

  if (err)
    return MACH_PORT_NULL;

  err = device_open (device_master, D_WRITE | D_READ, "console", &console);
  if (err)
    return MACH_PORT_NULL;

  return console;
}

static int open_console (char **namep);

int
main (int argc, char **argv)
{
  mach_port_t consdev = get_console ();
  mach_port_t runsystem;
  char *consname;

  if (consdev == MACH_PORT_NULL)
    _exit (127);

  stdin = stdout = NULL;
  stderr = mach_open_devstream (consdev, "w");
  if (!stderr)
    _exit (127);

  if (argc < 2)
    error (1, 0, "Usage: %s PROGRAM [ARG...]", program_invocation_short_name);

  /* Check whether runsystem exists before opening a console for it.  */
  runsystem = file_name_lookup (argv[1], O_RDONLY, 0);
  if (runsystem == MACH_PORT_NULL)
    error (127, errno, "cannot open file `%s' for execution", argv[1]);
  mach_port_deallocate (mach_task_self (), runsystem);

  if (open_console (&consname))
    setenv ("FALLBACK_CONSOLE", consname, 1);

  execv (argv[1], &argv[1]);
  error (5, errno, "cannot execute %s", argv[1]);
  /* NOTREACHED */
  return 127;
}

/* Open /dev/console.  If it isn't there, or it isn't a terminal, then
   create /dev/console and put the terminal on it.  If we get EROFS,
   in trying to create /dev/console then as a last resort, create
   /tmp/console.  If all fail, we exit.

   Return nonzero if the vanilla open of /dev/console didn't work.
   In any case, after the console has been opened, put it on fds 0, 1, 2.  */
static int
open_console (char **namep)
{
#define TERMINAL_FIRST_TRY "/hurd/term\0/dev/console\0device\0console"
#define TERMINAL_SECOND_TRY "/hurd/term\0/tmp/console\0device\0console"
  mach_port_t term, proc;
  static char *termname;
  struct stat st;
  error_t err = 0;
  int fd;
  int fallback;

  termname = _PATH_CONSOLE;
  term = file_name_lookup (termname, O_RDWR, 0);
  if (term != MACH_PORT_NULL)
    err = io_stat (term, &st);
  else
    err = errno;
  if (err)
    {
      if (err != ENOENT)
	error (0, err, "%s", termname);
    }
  else if (st.st_fstype != FSTYPE_TERM)
    error (0, 0, "%s: Not a terminal", termname);

  fallback = (term == MACH_PORT_NULL || err || st.st_fstype != FSTYPE_TERM);
  if (fallback)
    /* Start the terminal server ourselves. */
    {
      size_t argz_len;		/* Length of args passed to translator.  */
      char *terminal;		/* Name of term translator.  */
      mach_port_t control;	/* Control port for term translator.  */
      int try;

      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type,
			 task_t task, void *cookie)
	{
	  term = file_name_lookup (termname, flags | O_CREAT|O_NOTRANS, 0666);
	  if (term == MACH_PORT_NULL)
	    {
	      error (0, errno, "%s", termname);
	      return errno;
	    }

	  *underlying = term;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}

      terminal = TERMINAL_FIRST_TRY;
      argz_len = sizeof TERMINAL_FIRST_TRY;
      for (try = 1; try < 3; ++try)
	{
	  if (try == 2)
	    {
	      terminal = TERMINAL_SECOND_TRY;
	      argz_len = sizeof TERMINAL_SECOND_TRY;
	    }

	  termname = terminal + strlen (terminal) + 1; /* first arg is name */

	  /* The callback to start_translator opens TERM as a side effect.  */
	  err =
	    fshelp_start_translator (open_node, NULL, terminal, terminal,
				     argz_len, 3000, &control);
	  if (err)
	    {
	      error (0, err, "%s", terminal);
	      continue;
	    }

	  err = file_set_translator (term, 0, FS_TRANS_SET, 0, 0, 0,
				       control, MACH_MSG_TYPE_COPY_SEND);
	  mach_port_deallocate (mach_task_self (), control);
	  if (err)
	    {
	      error (0, err, "%s", termname);
	      continue;
	    }
	  mach_port_deallocate (mach_task_self (), term);

	  /* Now repeat the open. */
	  term = file_name_lookup (termname, O_RDWR, 0);
	  if (term == MACH_PORT_NULL)
	    {
	      error (0, errno, "%s", termname);
	      continue;
	    }
	  err = io_stat (term, &st);
	  if (err)
	    {
	      error (0, err, "%s", termname);
	      term = MACH_PORT_NULL;
	      continue;
	    }
	  if (st.st_fstype != FSTYPE_TERM)
	    {
	      error (0, 0, "%s: Not a terminal", termname);
	      term = MACH_PORT_NULL;
	      continue;
	    }

	  if (term != MACH_PORT_NULL)
	    {
	      if (try == 1)
		/* We created /dev/console, started, and installed the
		   translator on it, so it really isn't a fallback
		   console.  */
		fallback = 0;
	      else
		error (0, 0, "Using temporary console %s", termname);
	      break;
	    }
	}

      if (term == MACH_PORT_NULL)
	error (2, 0, "Cannot start console terminal");
    }

  fd = openport (term, O_RDWR);
  if (fd < 0)
    error (3, errno, "Cannot open console");

  if (fd != 0)
    {
      dup2 (fd, 0);
      close (fd);
    }
  dup2 (0, 1);
  dup2 (0, 2);

  if (getsid (0) != getpid ())
    if (setsid () == -1)
      error (0, errno, "setsid");

  /* Set the console to our pgrp.  */
  tcsetpgrp (0, getpid ());

  /* Put us in a new login collection.  */
  proc = getproc ();
  proc_make_login_coll (proc);
  mach_port_deallocate (mach_task_self (), proc);

  *namep = termname;
  return fallback;
}
