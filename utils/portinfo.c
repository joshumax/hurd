/* Print information about a task's ports

   Copyright (C) 1996,97,98,99 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <stddef.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <version.h>

#include <mach.h>

#include <hurd.h>
#include <hurd/process.h>

#include <portinfo.h>
#include <portxlate.h>

const char *argp_program_version = STANDARD_HURD_VERSION (portinfo);

static const struct argp_option options[] = {
  {0,0,0,0,0, 1},
  {"verbose",	'v', 0, 0, "Give more detailed information"},
  {"members",   'm', 0, 0, "Show members of port-sets"},
  {"hex-names",	'x', 0, 0, "Show port names in hexadecimal"},
#if 0				/* XXX implement this */
  {"query-process", 'q', 0, 0, "Query the process itself for the identity of"
     " the ports in question -- requires the process be in a sane state"},
#endif
  {"hold", '*', 0, OPTION_HIDDEN},

  {0,0,0,0, "Selecting which names to show:", 2},
  {"receive",	'r', 0, 0, "Show ports with receive rights"},
  {"send",	's', 0, 0, "Show ports with send rights"},
  {"send-once",	'o', 0, 0, "Show ports with send once rights"},
  {"dead-names",'d', 0, 0, "Show dead names"},
  {"port-sets",	'p', 0, 0, "Show port sets"},

  {0,0,0,0, "Translating port names between tasks:", 3},
  {"translate", 't', "PID", 0, "Translate port names to process PID"},
  {"show-targets", 'h', 0, 0,
     "Print a header describing the target process" },
  {"no-translation-errors", 'E', 0, 0,
     "Don't display an error if a specified port can't be translated" },
#if 0
  {"search",    'a', 0, 0,  "Search all processes for the given ports"},
  {"target-receive",  'R', 0, 0,
     "Only show ports that translate into receive rights"},
  {"target-send",     'S', 0, 0,
     "Only show ports that translate into receive rights"},
  {"target-send-once",'O', 0, 0,
     "Only show ports that translate into receive rights"},
#endif

  {0}
};
static const char *args_doc = "PID [NAME...]";
static const char *doc =
"Show information about mach ports NAME... (default all ports) in process PID."
"\vIf no port NAMEs are given, all ports in process PID are reported (if"
" translation is used, then only those common to both processes).  NAMEs"
" may be specified in hexadecimal or octal by using a 0x or 0 prefix.";

/* Return the task corresponding to the user argument ARG, exiting with an
   appriate error message if we can't.  */
static task_t
parse_task (char *arg)
{
  error_t err;
  task_t task;
  char *arg_end;
  pid_t pid = strtoul (arg, &arg_end, 10);
  static process_t proc = MACH_PORT_NULL;

  if (*arg == '\0' || *arg_end != '\0')
    error (10, 0, "%s: Invalid process id", arg);

  if (proc == MACH_PORT_NULL)
    proc = getproc ();

  err = proc_pid2task (proc, pid, &task);
  if (err)
    error (11, err, "%s", arg);

  return task;
}

static volatile int hold = 0;

int
main (int argc, char **argv)
{
  error_t err;
  task_t task;
  int search = 0;
  unsigned show = 0;		/* what info we print */
  mach_port_type_t only = 0, target_only = 0; /* Which names to show */
  task_t xlate_task = MACH_PORT_NULL;
  int no_translation_errors = 0; /* inhibit complaints about bad names */
  struct port_name_xlator *xlator = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'v': show |= PORTINFO_DETAILS; break;
	case 'm': show |= PORTINFO_MEMBERS; break;
	case 'x': show |= PORTINFO_HEX_NAMES; break;

	case 'r': only |= MACH_PORT_TYPE_RECEIVE; break;
	case 's': only |= MACH_PORT_TYPE_SEND; break;
	case 'o': only |= MACH_PORT_TYPE_SEND_ONCE; break;
	case 'd': only |= MACH_PORT_TYPE_DEAD_NAME; break;
	case 'p': only |= MACH_PORT_TYPE_PORT_SET; break;

	case 'R': target_only |= MACH_PORT_TYPE_RECEIVE; break;
	case 'S': target_only |= MACH_PORT_TYPE_SEND; break;
	case 'O': target_only |= MACH_PORT_TYPE_SEND_ONCE; break;

	case 't': xlate_task = parse_task (arg); break;
	case 'a': search = 1; break;
	case 'E': no_translation_errors = 1; break;

	case '*':
	  hold = 1;
	  while (hold)
	    sleep (1);
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    /* The task  */
	    {
	      task = parse_task (arg);

	      if (only == 0)
		only = ~0;
	      if (target_only == 0)
		target_only = ~0;

	      if (xlate_task != MACH_PORT_NULL)
		{
		  if (search)
		    argp_error (state,
				"Both --search and --translate specified");
		  err = port_name_xlator_create (task, xlate_task, &xlator);
		  if (err)
		    error (13, err, "Cannot setup task translation");
		}

	      if (state->next == state->argc)
		/* No port names specified, print all of them.  */
		{
		  if (xlator)
		    err = print_xlated_task_ports_info (xlator, only,
							show, stdout);
		  else
		    err = print_task_ports_info (task, only, show, stdout);
		  if (err)
		    error (12, err, "%s", arg);
		}
	      break;
	    }

	  /* A port name  */
	  {
	    char *end;
	    mach_port_t name = strtoul (arg, &end, 0);
	    if (name == 0)
	      error (0, 0, "%s: Invalid port name", arg);
	    else
	      {
		if (xlator)
		  {
		    err = print_xlated_port_info (name, 0, xlator,
						  show, stdout);
		    if (err && no_translation_errors)
		      break;
		  }
		else
		  err = print_port_info (name, 0, task, show, stdout);
		if (err)
		  error (0, err, "%s", arg);
	      }
	  }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  exit (0);
}
