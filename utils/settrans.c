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
  {"force",       'f', 0, 0, "Set the translator even if one already exists"},
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {0, 0}
};
static char *args_doc = "NODE [TRANSLATOR ARG...]";

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

  /* Various option flags.  */
  int passive = 0, active = 0;
  int create = 0, force = 0, deref = 0, keep_active = 0;

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
	case 'f': force = 1; break;
	case 'k': keep_active = 1; break;
	case 'c': create = 1; break;
	case 'L': deref = 1; break;

	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc};

  argp_parse (&argp, argc, argv, 0, 0);

  if (!active && !passive)
    passive = 1;

  node =
    file_name_lookup(node_name,
		     (deref ? 0 : O_NOTRANS) | (create ? O_CREAT : 0),
		     0666);
  if (node == MACH_PORT_NULL)
    error(1, errno, "%s", node_name);

  if (active && argz_len > 0)
    {
      err = fshelp_start_translator (node, MACH_MSG_TYPE_COPY_SEND,
				     argz, argz, argz_len, 60000,
				     &active_control);
      if (err)
	error(4, err, "%s", argz);
    }

  if (force)
    /* Kill any existing translators.  */
    flags &= ~FS_TRANS_EXCL;

  err =
    file_set_translator(node,
			passive ? flags : 0,
			(active || !keep_active) ? flags : 0,
			0,
			argz, argz_len,
			active_control, MACH_MSG_TYPE_MOVE_SEND);
  if (err)
    error(5, err, "%s", node_name);

  exit(0);
}
