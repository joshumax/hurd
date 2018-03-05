/* Bless processes.

   Copyright (C) 2016 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <assert-backtrace.h>
#include <error.h>
#include <stdlib.h>
#include <hurd.h>
#include <mach.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (bless);

pid_t pid;

static const struct argp_option options[] =
{
  {0}
};

static const char args_doc[] = "PID";
static const char doc[] = "Bless the given process.  Such a process is "
  "considered an essential part of the operating system and is not "
  "terminated when switching runlevels.";

/* Parse our options...	 */
error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  char *end;

  switch (key)
    {
    case ARGP_KEY_ARG:
      if (state->arg_num > 0)
	argp_error (state, "Too many non option arguments");

      pid = strtol (arg, &end, 10);
      if (arg == end || *end != '\0')
        argp_error (state, "Malformed pid '%s'", arg);
      break;

    case ARGP_KEY_NO_ARGS:
      argp_usage (state);

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

const struct argp argp =
  {
  options: options,
  parser: parse_opt,
  args_doc: args_doc,
  doc: doc,
  };

int
main (int argc, char **argv)
{
  error_t err;
  process_t proc;

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  err = proc_pid2proc (getproc (), pid, &proc);
  if (err)
    error (1, err, "Could not get process for pid %d", pid);

  err = proc_mark_important (proc);
  if (err)
    error (1, err, "Could not mark process as important");

  err = mach_port_deallocate (mach_task_self (), proc);
  assert_perror_backtrace (err);

  return EXIT_SUCCESS;
}
