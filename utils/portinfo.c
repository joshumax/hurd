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
  {"translate", 't', "PID", 0, "Translate port names from process PID"},
  {"hold", '*', 0, OPTION_HIDDEN},

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
"If no port NAMEs are given, all ports in process PID are reported (if"
" translation is used, then only those common to both processes).  NAMEs"
" may be specified in hexadecimal or octal by using a 0x or 0 prefix.  When"
" translating, the port-type selection options apply to the"
" translated task, not the destination one.";

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

struct name_xlator
{
  /* The tasks between which we are translating port names.  */
  mach_port_t from_task;
  mach_port_t to_task;

  /* True if we're translating receive rights in FROM_TASK; otherwise, we're
     translating send rights.  */
  int from_is_receive;

  /* Arrays of port names and type masks from TO_TASK, fetched by
     mach_port_names.  These are vm_allocated.  */
  mach_port_t *to_names;
  mach_msg_type_number_t to_names_len;
  mach_port_type_t *to_types;
  mach_msg_type_number_t to_types_len;

  /* An array of rights in the current task to the ports in TO_NAMES/TO_TASK,
     or MACH_PORT_NULL, indicating that none has been fetched yet.
     This vector is malloced.  */
  mach_port_t *ports;
};

static error_t 
name_xlator_create (mach_port_t from_task, mach_port_t to_task,
		    struct name_xlator **xlator)
{
  error_t err;
  struct name_xlator *x = malloc (sizeof (struct name_xlator));

  if (! x)
    return ENOMEM;

  x->from_task = from_task;
  x->to_task = to_task;
  x->to_names = 0;
  x->to_types = 0;
  x->to_names_len = 0;
  x->to_types_len = 0;

  /* Cache a list of names in TO_TASK.  */
  err = mach_port_names (to_task,
			 &x->to_names, &x->to_names_len,
			 &x->to_types, &x->to_types_len);

  if (! err)
    /* Make an array to hold ports from TO_TASK which have been translated
       into our namespace.  */
    {
      x->ports = malloc (sizeof (mach_port_t) * x->to_names_len);
      if (x->ports)
	{
	  int i;
	  for (i = 0; i < x->to_names_len; i++)
	    x->ports[i] = MACH_PORT_NULL;
	}
      else
	{
	  vm_deallocate (mach_task_self (),
			 (vm_address_t)x->to_names,
			 x->to_names_len * sizeof (mach_port_t));
	  vm_deallocate (mach_task_self (),
			 (vm_address_t)x->to_types,
			 x->to_types_len * sizeof (mach_port_type_t));
	  err = ENOMEM;
	}
    }

  if (err)
    free (x);
  else
    *xlator = x;

  return err;
}

static void
name_xlator_free (struct name_xlator *x)
{
  int i;

  for (i = 0; i < x->to_names_len; i++)
    if (x->ports[i] != MACH_PORT_NULL)
      mach_port_deallocate (mach_task_self (), x->ports[i]);
  free (x->ports);

  vm_deallocate (mach_task_self (),
		 (vm_address_t)x->to_names,
		 x->to_names_len * sizeof (mach_port_t));
  vm_deallocate (mach_task_self (),
		 (vm_address_t)x->to_types,
		 x->to_types_len * sizeof (mach_port_type_t));

  mach_port_deallocate (mach_task_self (), x->to_task);
  mach_port_deallocate (mach_task_self (), x->from_task);

  free (x);
}

/* Translate the port FROM between the tasks in X, returning the translated
   name in TO, and the types of TO in TO_TYPE, or an error.  If TYPE is
   non-zero, it should be what mach_port_type returns for FROM.  */
static error_t
name_xlator_xlate (struct name_xlator *x,
		   mach_port_t from, mach_port_type_t from_type,
		   mach_port_t *to, mach_port_type_t *to_type)
{
  error_t err;
  mach_port_t port;
  mach_msg_type_number_t i;
  mach_port_type_t aquired_type;
  mach_port_type_t valid_to_types;

  if (from_type == 0)
    {
      error_t err = mach_port_type (x->from_task, from, &from_type);
      if (err)
	return err;
    }

  if (from_type & MACH_PORT_TYPE_RECEIVE)
    valid_to_types = MACH_PORT_TYPE_SEND;
  else if (from_type & MACH_PORT_TYPE_SEND)
    valid_to_types = MACH_PORT_TYPE_SEND | MACH_PORT_TYPE_RECEIVE;
  else
    return EKERN_INVALID_RIGHT;

  /* Translate the name FROM, in FROM_TASK's namespace into our namespace. */
  err = 
    mach_port_extract_right (x->from_task, from,
			     ((from_type & MACH_PORT_TYPE_RECEIVE)
			      ? MACH_MSG_TYPE_MAKE_SEND
			      : MACH_MSG_TYPE_COPY_SEND),
			     &port,
			     &aquired_type);

  if (err)
    return err;

  /* Look for likely candidates in TO_TASK's namespace to test against PORT. */
  for (i = 0; i < x->to_names_len; i++)
    {
      if (x->ports[i] == MACH_PORT_NULL && (x->to_types[i] & valid_to_types))
	/* Port I shows possibilities... */
	{
	  err =
	    mach_port_extract_right (x->to_task,
				     x->to_names[i],
				     ((x->to_types[i] & MACH_PORT_TYPE_RECEIVE)
				      ? MACH_MSG_TYPE_MAKE_SEND
				      : MACH_MSG_TYPE_COPY_SEND),
				     &x->ports[i],
				     &aquired_type);
	  if (err)
	    x->to_types[i] = 0;	/* Don't try to fetch this port again.  */
	}

      if (x->ports[i] == port)
	/* We win!  Port I in TO_TASK is the same as PORT.  */
	break;
  }

  mach_port_deallocate (mach_task_self (), port);

  if (i < x->to_names_len)
    /* Port I is the right translation; return its name in TO_TASK.  */
    {
      *to = x->to_names[i];
      *to_type = x->to_types[i];
      return 0;
    }
  else
    return EKERN_INVALID_NAME;
}

/* Prints info in SHOW about NAME translated through X to stdout.  If TYPE is
   non-zero, it should be what mach_port_type returns for NAME.  */
static error_t
xlated_port_info (mach_port_t name, mach_port_type_t type,
		  struct name_xlator *x, unsigned show)
{
  mach_port_t old_name = name;
  error_t err = name_xlator_xlate (x, name, type, &name, &type);
  if (! err)
    {
      printf ((show & SHOW_HEX_NAMES) ? "%#6x => " : "%6d => ", old_name);
      err = port_info (name, type, x->to_task, show);
    }
  return err;
}

/* Prints info about every port common to both tasks in X, but only if the
   port in X->from_task has a type in ONLY, to stdout.  */
static error_t
xlated_ports_info (struct name_xlator *x, mach_port_type_t only, unsigned show)
{
  mach_port_t *names = 0;
  mach_port_type_t *types = 0;
  mach_msg_type_number_t names_len = 0, types_len = 0, i;
  error_t err =
    mach_port_names (x->from_task, &names, &names_len, &types, &types_len);

  if (err)
    return err;

  for (i = 0; i < names_len; i++)
    if (types[i] & only)
      xlated_port_info (names[i], types[i], x, show);

  vm_deallocate (mach_task_self (),
		 (vm_address_t)names, names_len * sizeof *names);
  vm_deallocate (mach_task_self (),
		 (vm_address_t)types, types_len * sizeof *types);

  return 0;
}

/* Return the task corresponding to the user argument ARG, exiting with an
   appriate error message if we can't.  */
static task_t
parse_task (char *arg)
{
  error_t err;
  task_t task;
  pid_t pid = atoi (arg);
  static process_t proc = MACH_PORT_NULL;

  if (proc == MACH_PORT_NULL)
    proc = getproc ();

  if (! pid)
    error (10, 0, "%s: Invalid process id", arg);

  err = proc_pid2task (proc, pid, &task);
  if (err)
    error (11, err, "%s", arg);

  return task;
}

static volatile hold = 0;

int
main (int argc, char **argv)
{
  error_t err;
  task_t task;
  unsigned show = 0;		/* what info we print */
  mach_port_type_t only = 0;	/* Which names to show */
  task_t xlate_task = MACH_PORT_NULL;
  struct name_xlator *xlator = 0;

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

	case 't': xlate_task = parse_task (arg); break;

	case '*':
	  hold = 1;
	  while (hold)
	    sleep (1);
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_error (state, "No process specified");
	  return ED;		/* Some non-EINVAL error.  */

	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    /* The task  */
	    {
	      task = parse_task (arg);

	      if (only == 0)
		only = ~0;

	      if (xlate_task != MACH_PORT_NULL)
		{
		  err = name_xlator_create (xlate_task, task, &xlator);
		  if (err)
		    error (13, err, "Cannot setup task translation");
		}

	      if (state->next == state->argc)
		/* No port names specified, print all of them.  */
		{
		  if (xlator)
		    err = xlated_ports_info (xlator, only, show);
		  else
		    err = ports_info (task, only, show);
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
		  err = xlated_port_info (name, 0, xlator, show);
		else
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
  argp_parse (&argp, argc, argv, 0, 0, 0);

  exit (0);
}
