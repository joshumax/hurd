/* Print information about a task's ports

   Copyright (C) 1996 Free Software Foundation, Inc.

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

#include <mach.h>

#include <hurd.h>
#include <hurd/process.h>

static const struct argp_option options[] = {
  {0,0,0,0,0, 1},
  {"verbose",	'v', 0, 0, "Give more detailed information"},
  {"members",   'm', 0, 0, "Show members of port-sets"},
  {"hex-names",	'x', 0, 0, "Show port names in hexadecimal"},

  {0,0,0,0, "Selecting which names to show:", 2},
  {"receive",	'r', 0, 0, "Show ports with receive rights"},
  {"send",	's', 0, 0, "Show ports with send rights"},
  {"send-once",	'o', 0, 0, "Show ports with send once rights"},
  {"dead-names",'d', 0, 0, "Show ports with dead name rights"},
  {"port-sets",	'p', 0, 0, "Show port sets"},

  {0}
};
static const char *args_doc = "PID [NAME...]";
static const char *doc =
"If no port NAMEs are given, all ports in process PID are reported.  NAMEs"
" may be specified in hexadecimal or octal by using a 0x or 0 prefix.";

#define SHOW_DETAILS	0x1
#define SHOW_MEMBERS	0x4
#define SHOW_HEX_NAMES	0x8

/* Prints info in SHOW about NAME in TASK to stdout.  If TYPE is non-zero, it
   should be what mach_port_type returns for NAME.  */
static error_t
port_info (mach_port_t name, mach_port_type_t type, task_t task, unsigned show)
{
  int hex_names = (show & SHOW_HEX_NAMES);
  int first = 1;
  void comma ()
    {
      if (first)
	first = 0;
      else
	printf (", ");
    }
  void prefs (mach_port_right_t right)
    {
      mach_port_urefs_t refs;
      error_t err = mach_port_get_refs (task, name, right, &refs);
      if (! err)
	printf (" (refs: %u)", refs);
    }

  if (type == 0)
    {
      error_t err = mach_port_type (task, name, &type);
      if (err)
	return err;
    }

  printf (hex_names ? "%#6x: " : "%6d: ", name);

  if (type & MACH_PORT_TYPE_RECEIVE)
    {
      comma ();
      printf ("receive");
      if (show & SHOW_DETAILS)
	{
	  struct mach_port_status status;
	  error_t err = mach_port_get_receive_status (task, name, &status);
	  if (! err)
	    {
	      printf (" (");
	      if (status.mps_pset != MACH_PORT_NULL)
		printf (hex_names ? "port-set: %#x, " : "port-set: %d, ",
			status.mps_pset);
	      printf ("seqno: %u", status.mps_seqno);
	      if (status.mps_mscount)
		printf (", ms-count: %u", status.mps_mscount);
	      if (status.mps_qlimit != MACH_PORT_QLIMIT_DEFAULT)
		printf (", qlimit: %u", status.mps_qlimit);
	      if (status.mps_msgcount)
		printf (", msgs: %u", status.mps_msgcount);
	      printf ("%s%s%s)",
		      status.mps_srights ? ", send-rights" : "",
		      status.mps_pdrequest ? ", pd-req" : "",
		      status.mps_nsrequest ? ", ns-req" : "");
	    }
	}
    }
  if (type & MACH_PORT_TYPE_SEND)
    {
      comma ();
      printf ("send");
      if (show & SHOW_DETAILS)
	prefs (MACH_PORT_RIGHT_SEND);
    }
  if (type & MACH_PORT_TYPE_SEND_ONCE)
    {
      comma ();
      printf ("send-once");
    }
  if (type & MACH_PORT_TYPE_DEAD_NAME)
    {
      comma ();
      printf ("dead-name");
      if (show & SHOW_DETAILS)
	prefs (MACH_PORT_RIGHT_DEAD_NAME);
    }
  if (type & MACH_PORT_TYPE_PORT_SET)
    {
      comma ();
      printf ("port-set");
      if (show & SHOW_DETAILS)
	{
	  mach_port_t *members = 0;
	  mach_msg_type_number_t members_len = 0, i;
	  error_t err =
	    mach_port_get_set_status (task, name, &members, &members_len);
	  if (! err)
	    if (members_len == 0)
	      printf (" (empty)");
	    else
	      {
		printf (hex_names ? " (%#x" : " (%u", members[0]);
		for (i = 1; i < members_len; i++)
		  printf (hex_names ? ", %#x" : ", %u", members[i]);
		printf (")");
		vm_deallocate (mach_task_self (), (vm_address_t)members,
			       members_len * sizeof *members);
	      }
	  }
    }
  putchar ('\n');

  return 0;
}

/* Prints info about every port in TASK with TYPES to stdout.  */
static error_t
ports_info (task_t task, mach_port_type_t only, unsigned show)
{
  mach_port_t *names = 0;
  mach_port_type_t *types = 0;
  mach_msg_type_number_t names_len = 0, types_len = 0, i;
  error_t err = mach_port_names (task, &names, &names_len, &types, &types_len);

  if (err)
    return err;

  for (i = 0; i < names_len; i++)
    if (types[i] & only)
      port_info (names[i], types[i], task, show);

  vm_deallocate (mach_task_self (),
		 (vm_address_t)names, names_len * sizeof *names);
  vm_deallocate (mach_task_self (),
		 (vm_address_t)types, types_len * sizeof *types);

  return 0;
}

int
main (int argc, char **argv)
{
  error_t err;
  task_t task;
  unsigned show = 0;		/* what info we print */
  mach_port_type_t only = 0;	/* Which names to show */

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'v': show |= SHOW_DETAILS; break;
	case 'm': show |= SHOW_MEMBERS; break;
	case 'x': show |= SHOW_HEX_NAMES; break;

	case 'r': only |= MACH_PORT_TYPE_RECEIVE; break;
	case 's': only |= MACH_PORT_TYPE_SEND; break;
	case 'o': only |= MACH_PORT_TYPE_SEND_ONCE; break;
	case 'd': only |= MACH_PORT_TYPE_DEAD_NAME; break;
	case 'p': only |= MACH_PORT_TYPE_PORT_SET; break;

	case ARGP_KEY_NO_ARGS:
	  argp_error (state->argp, "No process specified");

	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    /* The task  */
	    {
	      pid_t pid = atoi (arg);
	      if (! pid)
		error (10, 0, "%s: Invalid PID", arg);

	      err = proc_pid2task (getproc (), pid, &task);
	      if (err)
		error (11, err, "%s", arg);

	      if (only == 0)
		only = ~0;

	      if (state->next == state->argc)
		/* No port names specified, print all of them.  */
		{
		  err = ports_info (task, only, show);
		  if (err)
		    error (12, err, "%s", arg);
		}
	      break;
	    }

	  /* A port name  */
	  {
	    char *end;
	    unsigned long name = strtoul (arg, &end, 0);
	    if (name == 0)
	      error (0, 0, "%s: Invalid port name", arg);
	    else
	      {
		err = port_info (name, 0, task, show);
		if (err)
		  error (0, err, "%s", arg);
	      }
	  }
	  break;

	default: return EINVAL;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0);

  exit (0);
}
