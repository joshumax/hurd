/* Set a file's translator.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

static struct argp_option options[] =
{
  {"active",      'a', 0, 0, "Set NODE's active translator"},
  {"passive",     'p', 0, 0, "Set NODE's passive translator"},
  {"keep-active", 'k', 0, 0, "Keep any currently running active translator"
                             " when setting the passive translator"},
  {"create",      'c', 0, 0, "Create NODE if it doesn't exist"},
  {"goaway",      'g', 0, 0, "Make any existing translator go away"},
  {"recursive",   'R', 0, 0, "When killing an old translator, shutdown its children too"},
  {"force",       'f', 0, 0, "If an old active translator doesn't want to die, force it"},
  {"nosync",      'S', 0, 0, "Don't sync any existing translator's state before killing it"},
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {0, 0}
};
static char *args_doc = "NODE [TRANSLATOR ARG...]";
static char *doc = "By default, the passive translator is set, and any \
active translator told to just go away.";

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

  /* Flags to pass to file_set_translator.  By default we only set a
     translator if there's no existing one.  */
  int flags = FS_TRANS_SET | FS_TRANS_EXCL;
  int lookup_flags = O_NOTRANS;
  int goaway_flags = 0;

  /* Various option flags.  */
  int passive = 0, active = 0, keep_active = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  node_name = arg;
	  err = argz_create (state->argv + state->index, &argz, &argz_len);
	  if (err)
	    error(3, err, "Can't create options vector");
	  state->index = state->argc; /* stop parsing */
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state->argp); /* exits */

	case 'a': active = 1; break;
	case 'p': passive = 1; break;
	case 'k': keep_active = 1; break;

	case 'c': lookup_flags |= O_CREAT; break;
	case 'L': lookup_flags &= ~O_NOTRANS; break;

	case 'g': flags &= ~FS_TRANS_EXCL; break;

	case 'R': goaway_flags |= FSYS_GOAWAY_RECURSE; break;
	case 'S': goaway_flags |= FSYS_GOAWAY_NOSYNC; break;
	case 'f': goaway_flags |= FSYS_GOAWAY_FORCE; break;

	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0);

  if (!active && !passive)
    passive = 1;

  if (active && argz_len > 0)
    {
      /* The callback to start_translator opens NODE as a side effect.  */
      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type)
	{
	  node = file_name_lookup (node_name, flags | lookup_flags, 0666);
	  if (node == MACH_PORT_NULL)
	    return errno;

	  *underlying = node;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}
      err = fshelp_start_translator (open_node, argz, argz, argz_len, 60000,
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
			passive ? flags : 0,
			(active || !keep_active) ? flags : 0,
			goaway_flags,
			argz, argz_len,
			active_control, MACH_MSG_TYPE_MOVE_SEND);
  if (err)
    error(5, err, "%s", node_name);

  exit(0);
}
