/* opts-std-startup.c - Standard startup-time command line parser.
   Copyright (C) 1995,96,97,98,99,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org> and Marcus Brinkmann.

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

#include <stdio.h>
#include <argp.h>

#include "priv.h"


/* Option keys for long-only options in diskfs_common_options.  */
#define OPT_SLACK		600	/* --slack */
#define OPT_JUMP_DOWN_AT_INPUT	601	/* --jump-down-at-input */
#define OPT_JUMP_DOWN_AT_OUTPUT	602	/* --jump-down-at-output */

/* Common value for diskfs_common_options and diskfs_default_sync_interval. */
#define DEFAULT_SLACK 100
#define DEFAULT_SLACK_STRING STRINGIFY(DEFAULT_SLACK)
#define STRINGIFY(x) STRINGIFY_1(x)
#define STRINGIFY_1(x) #x

/* Number of records the client is allowed to lag behind the
   server.  */
int _cons_slack = DEFAULT_SLACK;

/* If we jump down at input.  */
int _cons_jump_down_at_input;

/* If we jump down at output.  */
int _cons_jump_down_at_output;

/* The filename of the console server.  */
char *_cons_file;

static const struct argp_option
startup_options[] =
{
  { "slack", OPT_SLACK, "RECORDS", 0, "Max number of records the client is"
    " allowed to lag behind the server (default " DEFAULT_SLACK_STRING ")" },
  { "jump-down-at-input", OPT_JUMP_DOWN_AT_INPUT, NULL, 0,
    "End scrollback when something is entered" },
  { "jump-down-at-output", OPT_JUMP_DOWN_AT_OUTPUT, NULL, 0,
    "End scrollback when something is printed" },
  { 0, 0 }
};

static const char args_doc[] = "CONSOLE";
static const char doc[] = "A console client.";


static error_t
parse_startup_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case OPT_SLACK:
      _cons_slack = atoi (arg);
      break;

    case OPT_JUMP_DOWN_AT_INPUT:
      _cons_jump_down_at_input = 1;
      break;

    case OPT_JUMP_DOWN_AT_OUTPUT:
      _cons_jump_down_at_output = 1;
      break;

    case ARGP_KEY_ARG:
      if (state->arg_num > 0)
	/* Too many arguments.  */
	argp_usage (state);

      _cons_file = arg;
      break;

    case ARGP_KEY_END:
      if (state->arg_num != 1)
	/* Not enough arguments. */
	argp_usage (state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

/* An argp structure for the standard console client command line
   arguments.  */
const struct argp
cons_startup_argp =
{
  startup_options, parse_startup_opt, args_doc, doc
};
