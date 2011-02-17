/* Set a file's translator.

   Copyright (C) 1995,96,97,98,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <error.h>
#include <fcntl.h>
#include <unistd.h>

#include <error.h>
#include <argz.h>
#include <hurd/fshelp.h>
#include <hurd/process.h>
#include <version.h>

#include <hurd/lookup.h>
#include <hurd/fsys.h>


const char *argp_program_version = STANDARD_HURD_VERSION (settrans);

#define DEFAULT_TIMEOUT 60

#define _STRINGIFY(arg) #arg
#define STRINGIFY(arg) _STRINGIFY (arg)

static struct argp_option options[] =
{
  {"active",      'a', 0, 0, "Start and set NODE's active translator" },
  {"passive",     'p', 0, 0, "Change NODE's passive translator record (default)" },
  {"create",      'c', 0, 0, "Create NODE if it doesn't exist" },
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {"pause",       'P', 0, 0, "When starting an active translator, prompt and"
     " wait for a newline on stdin before completing the startup handshake"},
  {"timeout",     't',"SEC",0, "Timeout for translator startup, in seconds"
     " (default " STRINGIFY (DEFAULT_TIMEOUT) "); 0 means no timeout"},
  {"exclusive",   'x', 0, 0, "Only set the translator if there is not one already"},
  {"orphan",      'o', 0, 0, "Disconnect old translator from the filesystem "
			     "(do not ask it to go away)"},

  {"chroot",      'C', 0, 0,
   "Instead of setting the node's translator, take following arguments up to"
   " `--' and run that command chroot'd to the translated node."},

  {0,0,0,0, "When setting the passive translator, if there's an active translator:"},
  {"goaway",      'g', 0, 0, "Ask the active translator to go away"},
  {"keep-active", 'k', 0, 0, "Leave any existing active translator running"},

  {0,0,0,0, "When an active translator is told to go away:"},
  {"recursive",   'R', 0, 0, "Shutdown its children too"},
  {"force",       'f', 0, 0, "Ask it to ignore current users and shutdown "
			     "anyway." },
  {"nosync",      'S', 0, 0, "Don't sync it before killing it"},

  {0, 0}
};
static char *args_doc = "NODE [TRANSLATOR ARG...]";
static char *doc = "Set the passive/active translator on NODE."
"\vBy default the passive translator is set.";

/* ---------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
  error_t err;

  /* The filesystem node we're putting a translator on.  */
  char *node_name = 0;
  file_t node;

  /* The translator's arg vector, in '\0' separated format.  */
  char *argz = 0;
  size_t argz_len = 0;

  /* The control port for any active translator we start up.  */
  fsys_t active_control = MACH_PORT_NULL;

  /* Flags to pass to file_set_translator.  */
  int active_flags = 0;
  int passive_flags = 0;
  int lookup_flags = O_NOTRANS;
  int goaway_flags = 0;

  /* Various option flags.  */
  int passive = 0, active = 0, keep_active = 0, pause = 0, kill_active = 0,
      orphan = 0;
  int excl = 0;
  int timeout = DEFAULT_TIMEOUT * 1000; /* ms */
  char **chroot_command = 0;

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
	case 'o': orphan = 1; break;

	case 'C':
	  if (chroot_command)
	    {
	      argp_error (state, "--chroot given twice");
	      return EINVAL;
	    }
	  chroot_command = &state->argv[state->next];
	  while (state->next < state->argc)
	    {
	      if (!strcmp (state->argv[state->next], "--"))
		{
		  state->argv[state->next++] = 0;
		  if (chroot_command[0] == 0)
		    {
		      argp_error (state,
				  "--chroot must be followed by a command");
		      return EINVAL;
		    }
		  return 0;
		}
	      ++state->next;
	    }
	  argp_error (state, "--chroot command must be terminated with `--'");
	  return EINVAL;

	case 'c': lookup_flags |= O_CREAT; break;
	case 'L': lookup_flags &= ~O_NOTRANS; break;

	case 'R': goaway_flags |= FSYS_GOAWAY_RECURSE; break;
	case 'S': goaway_flags |= FSYS_GOAWAY_NOSYNC; break;
	case 'f': goaway_flags |= FSYS_GOAWAY_FORCE; break;

	  /* Use atof so the user can specifiy fractional timeouts.  */
	case 't': timeout = atof (arg) * 1000.0; break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  if (!active && !passive && !chroot_command)
    passive = 1;		/* By default, set the passive translator.  */

  if (passive)
    passive_flags = FS_TRANS_SET | (excl ? FS_TRANS_EXCL : 0);
  if (active)
    active_flags = FS_TRANS_SET | (excl ? FS_TRANS_EXCL : 0)
		   | (orphan ? FS_TRANS_ORPHAN : 0);

  if (passive && !active)
    {
      /* When setting just the passive, decide what to do with any active.  */
      if (kill_active)
	/* Make it go away.  */
	active_flags = FS_TRANS_SET;
      else if (! keep_active)
	/* Ensure that there isn't one.  */
	active_flags = FS_TRANS_SET | FS_TRANS_EXCL;
    }

  if ((active || chroot_command) && argz_len > 0)
    {
      /* Error during file lookup; we use this to avoid duplicating error
	 messages.  */
      error_t open_err = 0;

      /* The callback to start_translator opens NODE as a side effect.  */
      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type,
			 task_t task, void *cookie)
	{
	  if (pause)
	    {
	      fprintf (stderr, "Translator pid: %d\nPausing...",
	               task2pid (task));
	      getchar ();
	    }

	  node = file_name_lookup (node_name, flags | lookup_flags, 0666);
	  if (node == MACH_PORT_NULL)
	    {
	      open_err = errno;
	      return open_err;
	    }

	  *underlying = node;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}
      err = fshelp_start_translator (open_node, NULL, argz, argz, argz_len,
				     timeout, &active_control);
      if (err)
	/* If ERR is due to a problem opening the translated node, we print
	   that name, otherwise, the name of the translator.  */
	error(4, err, "%s", (err == open_err) ? node_name : argz);
    }
  else
    {
      node = file_name_lookup(node_name, lookup_flags, 0666);
      if (node == MACH_PORT_NULL)
	error(1, errno, "%s", node_name);
    }

  if (active || passive)
    {
      err = file_set_translator (node,
				 passive_flags, active_flags, goaway_flags,
				 argz, argz_len,
				 active_control, MACH_MSG_TYPE_COPY_SEND);
      if (err)
	error (5, err, "%s", node_name);
    }

  if (chroot_command)
    {
      /* We will act as the parent filesystem would for a lookup
	 of the active translator's root node, then use this port
	 as our root directory while we exec the command.  */

      char retry_name[1024];	/* XXX */
      retry_type do_retry;
      mach_port_t root;
      err = fsys_getroot (active_control,
			  MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND,
			  NULL, 0, NULL, 0, 0, &do_retry, retry_name, &root);
      mach_port_deallocate (mach_task_self (), active_control);
      if (err)
	error (6, err, "fsys_getroot");
      err = hurd_file_name_lookup_retry (&_hurd_ports_use, &getdport, 0,
					 do_retry, retry_name, 0, 0,
					 &root);
      if (err)
	error (6, err, "cannot resolve root port");

      if (setcrdir (root))
	error (7, errno, "cannot install root port");
      mach_port_deallocate (mach_task_self (), root);
      if (chdir ("/"))
	error (8, errno, "cannot chdir to new root");

      execvp (chroot_command[0], chroot_command);
      error (8, errno, "cannot execute %s", chroot_command[0]);
    }

  return 0;
}
