/* A minimalist init for the Hurd

   Copyright (C) 2013,14 Free Software Foundation, Inc.
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
   along with the Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <error.h>
#include <hurd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (init);
static pid_t child_pid;
static int single;

static struct argp_option
options[] =
{
  /* XXX: Currently, -s does nothing.  */
  {"single-user", 's', NULL, 0, "Startup system in single-user mode", 0},
  {NULL, 'a', NULL, 0, "Ignored for compatibility with sysvinit", 0},
  {0}
};

static char doc[] = "A minimalist init for the Hurd";

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 's':
      single = 1;
      break;

    case 'a':
      /* Ignored.  */
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

void
sigchld_handler(int signal)
{
  /* A child died.  Find its status.  */
  int status;
  pid_t pid;

  while (1)
    {
      pid = waitpid (WAIT_ANY, &status, WNOHANG | WUNTRACED);
      if (pid <= 0)
	break;		/* No more children.  */

      /* Since we are init, orphaned processes get reparented to us and
	 alas, all our adopted children eventually die.  Woe is us.  We
	 just need to reap the zombies to relieve the proc server of
	 its burden, and then we can forget about the little varmints.  */

      if (pid == child_pid)
	{
	  /* The big magilla bit the dust.  */
	  child_pid = -1;

	  char *desc = NULL;
	  if (WIFSIGNALED (status))
	    asprintf (&desc, "terminated abnormally (%s)",
		      strsignal (WTERMSIG (status)));
	  else if (WIFSTOPPED (status))
	    asprintf (&desc, "stopped abnormally (%s)",
		      strsignal (WTERMSIG (status)));
	  else if (WEXITSTATUS (status) == 0)
	    desc = strdup ("finished");
	  else
	    asprintf (&desc, "exited with status %d",
		      WEXITSTATUS (status));

	  error (0, 0, "child %s", desc);
	  free (desc);

	  /* XXX: launch emergency shell.  */
	  error (23, 0, "panic!!");
	}
    }
}

int
main (int argc, char **argv)
{
  struct argp argp =
    {
      .options = options,
      .parser = parse_opt,
      .doc = doc,
    };
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (getpid () != 1)
    error (1, 0, "can only be run as PID 1");

  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset (&sa.sa_mask);

  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGUSR1, &sa, NULL);
  sigaction (SIGUSR2, &sa, NULL);
  sigaction (SIGTSTP, &sa, NULL);

  sa.sa_handler = sigchld_handler;
  sa.sa_flags |= SA_RESTART;
  sigaction (SIGCHLD, &sa, NULL);

  char *args[] = { "/libexec/runsystem.hurd", NULL };

  switch (child_pid = fork ())
    {
    case -1:
      error (1, errno, "failed to fork");
    case 0:
      execv (args[0], args);
      error (2, errno, "failed to execv child %s", args[0]);
    }

  select (0, NULL, NULL, NULL, NULL);
  /* Not reached.  */
  return 0;
}
