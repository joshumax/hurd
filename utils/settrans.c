/* Set a file's translator.

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <fcntl.h>
#include <unistd.h>

#include <error.h>
#include <argz.h>
#include <hurd/fshelp.h>

#define DEFAULT_TIMEOUT 60

#define _STRINGIFY(arg) #arg
#define STRINGIFY(arg) _STRINGIFY (arg)

static struct argp_option options[] =
{
  {"active",      'a', 0, 0, "Set NODE's active translator", 1},
  {"passive",     'p', 0, 0, "Set NODE's passive translator"},
  {"keep-active", 'k', 0, 0, "Keep any currently running active translator"
     " when setting the passive translator"},
  {"create",      'c', 0, 0, "Create NODE if it doesn't exist"},
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {"goaway",      'g', 0, 0, "Make any existing active translator go away"
     " when setting the passive translator"},
  {"pause",       'P', 0, 0, "When starting an active translator, prompt and"
     " wait for a newline on stdin before completing the startup handshake"},
  {"timeout",     't',"SEC",0, "Timeout for translator startup, in seconds"
     " (default " STRINGIFY (DEFAULT_TIMEOUT) "); 0 means no timeout"},
  {"exclusive",   'x', 0, 0, "Only set the translator if there is none already"},

  {0,0,0,0, "When an active translator is told to go away:", 2},
  {"recursive",   'R', 0, 0, "Shutdown its children too"},
  {"force",       'f', 0, 0, "If it doesn't want to die, force it"},
  {"nosync",      'S', 0, 0, "Don't sync it before killing it"},

  {0, 0}
};
static char *args_doc = "NODE [TRANSLATOR ARG...]";
static char *doc = "By default the passive translator is set.";

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  error_t err;

  /* The filesystem node we're putting a translator on.  */
  char *node_name = 0;
  file_t node;

  /* The translator's arg vector, in '\0' separated format.  */
  char *argz = 0;
  int argz_len = 0;

  /* The control port for any active translator we start up.  */
  fsys_t active_control = MACH_PORT_NULL;

  /* Flags to pass to file_set_translator.  */
  int active_flags = 0;
  int passive_flags = 0;
  int lookup_flags = O_NOTRANS;
  int goaway_flags = 0;

  /* Various option flags.  */
  int passive = 0, active = 0, keep_active = 0, pause = 0, kill_active = 1;
  int excl = 0;
  int timeout = DEFAULT_TIMEOUT * 1000; /* ms */

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    node_name = arg;
	  else			/* command */
	    {
	      error_t err =
		argz_create (state->argv + state->next - 1, &argz, &argz_len);
	      if (err)
		error(3, err, "Can't create options vector");
	      state->next = state->argc; /* stop parsing */
	    }
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	case 'a': active = 1; break;
	case 'p': passive = 1; break;
	case 'k': keep_active = 1; break;
	case 'g': kill_active = 1; break;
	case 'x': excl = 1; break;
	case 'P': pause = 1; break;

	case 'c': lookup_flags |= O_CREAT; break;
	case 'L': lookup_flags &= ~O_NOTRANS; break;

	case 'R': goaway_flags |= FSYS_GOAWAY_RECURSE; break;
	case 'S': goaway_flags |= FSYS_GOAWAY_NOSYNC; break;
	case 'f': goaway_flags |= FSYS_GOAWAY_FORCE; break;

	  /* Use atof so the user can specifiy fractional timeouts.  */
	case 't': timeout = atof (arg) * 1000.0; break;

	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  if (!active && !passive)
    passive = 1;		/* By default, set the passive translator.  */

  if (passive)
    passive_flags = FS_TRANS_SET | (excl ? FS_TRANS_EXCL : 0);
  if (active)
    active_flags = FS_TRANS_SET | (excl ? FS_TRANS_EXCL : 0);

  if (passive && !active)
    /* When setting just the passive, decide what to do with any active.  */
    if (kill_active)
      /* Make it go away.  */
      active_flags = FS_TRANS_SET;
    else if (! keep_active)
      /* Ensure that there isn't one.  */
      active_flags = FS_TRANS_SET | FS_TRANS_EXCL;

  if (active && argz_len > 0)
    {
      /* The callback to start_translator opens NODE as a side effect.  */
      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type)
	{
	  if (pause)
	    {
	      fprintf (stderr, "Pausing...");
	      getchar ();
	    }

	  node = file_name_lookup (node_name, flags | lookup_flags, 0666);
	  if (node == MACH_PORT_NULL)
	    return errno;

	  *underlying = node;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}
      err = fshelp_start_translator (open_node, argz, argz, argz_len, timeout,
				     &active_control);
      if (err)
	error(4, err, "%s", argz);
    }
  else
    {
      node = file_name_lookup(node_name, lookup_flags, 0666);
      if (node == MACH_PORT_NULL)
	error(1, errno, "%s", node_name);
    }

  err =
    file_set_translator(node,
			passive_flags, active_flags, goaway_flags,
			argz, argz_len,
			active_control, MACH_MSG_TYPE_MOVE_SEND);
  if (err)
    error(5, err, "%s", node_name);

  exit(0);
}
