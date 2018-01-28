/* Utility to drop all authentication credentials.

   Copyright (C) 2013 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

   This file is part of the GNU Hurd.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <error.h>
#include <nullauth.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <version.h>

static char **args;

const char *argp_program_version = STANDARD_HURD_VERSION (nullauth);

static const struct argp_option options[] =
{
  { 0 }
};

static const char doc[] =
  "Drop all authentication credentials and run the given program.";
static const char args_doc[] =
  "PROGRAM [ARGUMENTS...]\tThe program to run";

error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_ARGS:
      args = state->argv + state->next;
      break;

    case ARGP_KEY_NO_ARGS:
      argp_error (state, "expected program to run");
      return EINVAL;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static struct argp argp = {
  options,
  parse_opt,
  args_doc,
  doc,
  NULL,
};

int
main (int argc, char *argv[])
{
  error_t err;

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, NULL);

  /* Drop all privileges.  */
  err = setnullauth();
  if (err)
    error (1, err, "Could not drop privileges");

  execv (args[0], args);
  error (1, errno, "execv");

  /* Not reached.  */
  return EXIT_FAILURE;
}
