/* Set a file's translator.

   Copyright (C) 1995,96,97,98,2001,02,13,14
     Free Software Foundation, Inc.
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

#include <assert-backtrace.h>
#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <error.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

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

#define OPT_CHROOT_CHDIR	-1
#define OPT_STACK		-2

static struct argp_option options[] =
{
  {"active",      'a', 0, 0, "Start TRANSLATOR and set it as NODE's active translator" },
  {"start",       's', 0, 0, "Start the translator specified by the NODE's passive translator record and set it as NODE's active translator" },
  {"passive",     'p', 0, 0, "Change NODE's passive translator record (default)" },
  {"create",      'c', 0, 0, "Create NODE if it doesn't exist" },
  {"dereference", 'L', 0, 0, "If a translator exists, put the new one on top"},
  {"pid-file",    'F', "FILENAME", 0, "When starting an active translator,"
     " write its pid to this file"},
  {"pause",       'P', 0, 0, "When starting an active translator, prompt and"
     " wait for a newline on stdin before completing the startup handshake"},
  {"timeout",     't',"SEC",0, "Timeout for translator startup, in seconds"
     " (default " STRINGIFY (DEFAULT_TIMEOUT) "); 0 means no timeout"},
  {"exclusive",   'x', 0, 0, "Only set the translator if there is not one already"},
  {"orphan",      'o', 0, 0, "Disconnect old translator from the filesystem "
			     "(do not ask it to go away)"},
  {"underlying",  'U', "NODE", 0, "Open NODE and hand it to the translator "
				  "as the underlying node"},
  {"stack", OPT_STACK, 0, 0, "Replace an existing translator, but keep it "
			     "running, and put the new one on top"},

  {"chroot",      'C', 0, 0,
   "Instead of setting the node's translator, take following arguments up to"
   " `--' and run that command chroot'd to the translated node."},
  {"chroot-chdir",      OPT_CHROOT_CHDIR, "DIR", 0,
   "Change to DIR before running the chrooted command.  "
   "DIR must be an absolute path."},

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

/* Authentication of the current process.  */
uid_t *uids;
gid_t *gids;
size_t uids_len, gids_len;

/* Initialize and populate the uids and gids vectors.  */
error_t
get_credentials (void)
{
  /* Fetch uids...  */
  uids_len = geteuids (0, 0);
  if (uids_len < 0)
    return errno;

  uids = malloc (uids_len * sizeof (uid_t));
  if (! uids)
    return ENOMEM;

  uids_len = geteuids (uids_len, uids);
  if (uids_len < 0)
    return errno;

  /* ... and gids.  */
  gids_len = getgroups (0, 0);
  if (gids_len < 0)
    return errno;

  gids = malloc (gids_len * sizeof (gid_t));
  if (! uids)
    return ENOMEM;

  gids_len = getgroups (gids_len, gids);
  if (gids_len < 0)
    return errno;

  return 0;
}

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
  int start = 0;
  int stack = 0;
  char *pid_file = NULL;
  int excl = 0;
  int timeout = DEFAULT_TIMEOUT * 1000; /* ms */
  char *underlying_node_name = NULL;
  int underlying_lookup_flags;
  char **chroot_command = 0;
  char *chroot_chdir = "/";

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
	      if (start)
		argp_error (state, "both --start and TRANSLATOR given");

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
	case 's':
	  start = 1;
	  active = 1;	/* start implies active */
	  break;
	case OPT_STACK:
	  stack = 1;
	  active = 1;	/* stack implies active */
	  orphan = 1;	/* stack implies orphan */
	  break;
	case 'p': passive = 1; break;
	case 'k': keep_active = 1; break;
	case 'g': kill_active = 1; break;
	case 'x': excl = 1; break;
	case 'P': pause = 1; break;
	case 'F':
	  pid_file = strdup (arg);
	  if (pid_file == NULL)
	    error(3, ENOMEM, "Failed to duplicate argument");
	  break;

	case 'o': orphan = 1; break;
	case 'U':
	  underlying_node_name = strdup (arg);
	  if (underlying_node_name == NULL)
	    error(3, ENOMEM, "Failed to duplicate argument");
	  break;

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

	case OPT_CHROOT_CHDIR:
	  if (arg[0] != '/')
	    argp_error (state, "--chroot-chdir must be absolute");
	  chroot_chdir = arg;
	  break;

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

  if (stack)
    {
      underlying_node_name = node_name;
      underlying_lookup_flags = lookup_flags && ~O_NOTRANS;
    }
  else
    underlying_lookup_flags = lookup_flags;

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

  if (start)
    {
      /* Retrieve the passive translator record in argz.  */
      mach_port_t node = file_name_lookup (node_name, lookup_flags, 0);
      if (node == MACH_PORT_NULL)
	error (4, errno, "%s", node_name);

      char buf[1024];
      argz = buf;
      argz_len = sizeof (buf);

      err = file_get_translator (node, &argz, &argz_len);
      if (err == EINVAL)
	error (4, 0, "%s: no passive translator record found", node_name);
      if (err)
	error (4, err, "%s", node_name);

      mach_port_deallocate (mach_task_self (), node);
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

	  if (pid_file != NULL)
	    {
	      FILE *h;
	      h = fopen (pid_file, "w");
	      if (h == NULL)
		error (4, errno, "Failed to open pid file");

	      fprintf (h, "%i\n", task2pid (task));
	      fclose (h);
	    }

	  node = file_name_lookup (node_name, flags | lookup_flags, 0666);
	  if (node == MACH_PORT_NULL)
	    {
	      open_err = errno;
	      return open_err;
	    }

	  if (underlying_node_name)
	    {
	      *underlying = file_name_lookup (underlying_node_name,
					      flags | underlying_lookup_flags,
					      0666);
	      if (! MACH_PORT_VALID (*underlying))
		{
		  /* For the error message.  */
		  node_name = underlying_node_name;
		  open_err = errno;
		  return open_err;
		}
	    }
	  else
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
      pid_t child;
      int status;
      switch ((child = fork ()))
	{
	case -1:
	  error (6, errno, "fork");

	case 0:; /* Child.  */
	  /* We will act as the parent filesystem would for a lookup
	     of the active translator's root node, then use this port
	     as our root directory while we exec the command.  */

	  char retry_name[1024];	/* XXX */
	  retry_type do_retry;
	  mach_port_t root;
	  file_t executable;
	  char *prefixed_name;

	  err = get_credentials ();
	  if (err)
	    error (6, err, "getting credentials");

	  err = fsys_getroot (active_control,
			      MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND,
			      uids, uids_len, gids, gids_len, 0,
			      &do_retry, retry_name, &root);
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
	  if (chdir (chroot_chdir))
	    error (8, errno, "%s", chroot_chdir);

	  /* Lookup executable in PATH.  */
	  executable = file_name_path_lookup (chroot_command[0],
					      getenv ("PATH"),
					      O_EXEC, 0,
					      &prefixed_name);
	  if (MACH_PORT_VALID (executable))
	    {
	      err = mach_port_deallocate (mach_task_self (), executable);
	      assert_perror_backtrace (err);
	      if (prefixed_name)
		chroot_command[0] = prefixed_name;
	    }

	  execvp (chroot_command[0], chroot_command);
	  error (8, errno, "cannot execute %s", chroot_command[0]);
	  break;

	default: /* Parent.  */
	  if (waitpid (child, &status, 0) != child)
	    error (8, errno, "waitpid on %d", child);

	  err = fsys_goaway (active_control, goaway_flags);
	  if (err && err != EBUSY)
	    error (9, err, "fsys_goaway");

	  if (WIFSIGNALED (status))
	    error (WTERMSIG (status) + 128, 0,
		   "%s for child %d", strsignal (WTERMSIG (status)), child);
	  if (WEXITSTATUS (status) != 0)
	    error (WEXITSTATUS (status), 0,
		   "Error %d for child %d", WEXITSTATUS (status), child);
	}
    }

  return 0;
}
