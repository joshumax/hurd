/* opts-std-startup.c - Standard startup-time command line parser.
   Copyright (C) 1995,96,97,98,99,2001,02,2003 Free Software Foundation, Inc.
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
#include <string.h>

#include "priv.h"


/* Option keys for long-only options in diskfs_common_options.  */
#define OPT_SLACK			600	/* --slack */
#define OPT_JUMP_DOWN_ON_INPUT		601	/* --jump-down-on-input */
#define OPT_NO_JUMP_DOWN_ON_INPUT	602	/* --no-jump-down-on-input */
#define OPT_JUMP_DOWN_ON_OUTPUT		603	/* --jump-down-on-output */
#define OPT_NO_JUMP_DOWN_ON_OUTPUT	604	/* --no-jump-down-on-output */
#define OPT_VISUAL_BELL			605	/* --visual-bell */
#define OPT_AUDIBLE_BELL		606	/* --audible-bell */

/* Common value for diskfs_common_options and diskfs_default_sync_interval. */
#define DEFAULT_SLACK 100
#define DEFAULT_SLACK_STRING STRINGIFY(DEFAULT_SLACK)
#define STRINGIFY(x) STRINGIFY_1(x)
#define STRINGIFY_1(x) #x

/* Number of records the client is allowed to lag behind the
   server.  */
int _cons_slack = DEFAULT_SLACK;

/* If we jump down on input.  */
int _cons_jump_down_on_input = 1;

/* If we jump down on output.  */
int _cons_jump_down_on_output;

/* The filename of the console server.  */
char *_cons_file;

/* The type of bell used for the visual bell.  */
bell_type_t _cons_visual_bell = BELL_VISUAL;

/* The type of bell used for the audible bell.  */
bell_type_t _cons_audible_bell = BELL_AUDIBLE;

static const struct argp_option
startup_options[] =
{
  { "slack", OPT_SLACK, "RECORDS", 0, "Max number of records the client is"
    " allowed to lag behind the server (default " DEFAULT_SLACK_STRING ")" },
  { "jump-down-on-input", OPT_JUMP_DOWN_ON_INPUT, NULL, 0,
    "End scrollback when something is entered (default)" },
  { "no-jump-down-on-input", OPT_NO_JUMP_DOWN_ON_INPUT, NULL, 0,
    "End scrollback when something is entered" },
  { "jump-down-on-output", OPT_JUMP_DOWN_ON_OUTPUT, NULL, 0,
    "End scrollback when something is printed" },
  { "no-jump-down-on-output", OPT_NO_JUMP_DOWN_ON_OUTPUT, NULL, 0,
    "End scrollback when something is printed (default)" },
  { "visual-bell", OPT_VISUAL_BELL, "BELL", 0, "Visual bell: on (default), "
    "off, visual, audible" },
  { "audible-bell", OPT_AUDIBLE_BELL, "BELL", 0, "Audible bell: on (default), "
    "off, visual, audible" },
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

    case OPT_JUMP_DOWN_ON_INPUT:
      _cons_jump_down_on_input = 1;
      break;

    case OPT_NO_JUMP_DOWN_ON_INPUT:
      _cons_jump_down_on_input = 0;
      break;

    case OPT_JUMP_DOWN_ON_OUTPUT:
      _cons_jump_down_on_output = 1;
      break;

    case OPT_NO_JUMP_DOWN_ON_OUTPUT:
      _cons_jump_down_on_output = 0;
      break;

    case OPT_AUDIBLE_BELL:
      if (!strcasecmp ("on", arg) || !strcasecmp ("audible", arg))
	_cons_audible_bell = BELL_AUDIBLE;
      else if (!strcasecmp ("off", arg))
	_cons_audible_bell = BELL_OFF;
      else if (!strcasecmp ("visual", arg))
	_cons_audible_bell = BELL_VISUAL;
      else
	argp_error (state, "The audible bell can be one of: on, off, visual, "
		    "audible");
      break;

    case OPT_VISUAL_BELL:
      if (!strcasecmp ("on", arg) || !strcasecmp ("visual", arg))
	_cons_visual_bell = BELL_VISUAL;
      else if (!strcasecmp ("off", arg))
	_cons_visual_bell = BELL_OFF;
      else if (!strcasecmp ("audible", arg))
	_cons_visual_bell = BELL_AUDIBLE;
      else
	argp_error (state, "The visual bell can be one of: on, off, visual, "
		    "audible");
      break;
      
    case ARGP_KEY_ARG:
      if (state->arg_num > 0)
	/* Too many arguments.  */
	argp_error (state, "Too many non option arguments");
      _cons_file = arg;
      break;

    case ARGP_KEY_NO_ARGS:
      argp_error (state, "Filename of console server missing");
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
