/* Pid parsing/frobbing

   Copyright (C) 1997,99,2002 Free Software Foundation, Inc.
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <argp.h>
#include <hurd.h>
#include <hurd/process.h>
#include <mach.h>
#include <sys/mman.h>

#include "parse.h"
#include "pids.h"

static process_t _proc_server = MACH_PORT_NULL;

/* Return this process's proc server. */
static inline process_t
proc_server ()
{
  if (_proc_server == MACH_PORT_NULL)
    _proc_server = getproc ();
  return _proc_server;
}

/* Add the pids returned in vm_allocated memory by calling PIDS_FN with ID as
   an argument to PIDS and NUM_PIDS, reallocating it in malloced memory.  */
error_t
add_fn_pids (pid_t **pids, size_t *num_pids, unsigned id,
	     error_t (*pids_fn)(process_t proc, pid_t id,
				pid_t **pids, size_t *num_pids))
{
  size_t num_new_pids = 25;
  pid_t _new_pids[num_new_pids], *new_pids = _new_pids;
  error_t err = (*pids_fn)(proc_server (), id, &new_pids, &num_new_pids);

  if (! err)
    {
      size_t new_sz = *num_pids + num_new_pids;
      pid_t *new = realloc (*pids, new_sz * sizeof (pid_t));
      if (new)
	{
	  bcopy (new_pids, new + (*num_pids * sizeof (pid_t)),
		 num_new_pids * sizeof (pid_t));
	  *pids = new;
	  *num_pids = new_sz;
	}
      else
	err = ENOMEM;
      if (new_pids != _new_pids)
	munmap (new_pids, num_new_pids * sizeof (pid_t));
    }

  return err;
}

/* Add PID to PIDS and NUM_PIDS, reallocating it in malloced memory.  */
error_t
add_pid (pid_t **pids, size_t *num_pids, pid_t pid)
{
  size_t new_sz = *num_pids + 1;
  pid_t *new = realloc (*pids, new_sz * sizeof (pid_t));

  if (new)
    {
      new[new_sz - 1] = pid;
      *pids = new;
      *num_pids = new_sz;
      return 0;
    }
  else
    return ENOMEM;
}

struct pids_parse_state
{
  struct pids_argp_params *params;
  struct argp_state *state;
};

/* Returns our session id.  */
static pid_t
current_sid (struct argp_state *state)
{
  pid_t sid = -1;
  error_t err = proc_getsid (proc_server (), getpid (), &sid);
  if (err)
    argp_failure (state, 2, err, "Couldn't get current session id");
  return sid;
}

/* Returns our login collection id.  */
static pid_t
current_lid (struct argp_state *state)
{
  pid_t lid = -1;
  error_t err = proc_getloginid (proc_server (), getpid (), &lid);
  if (err)
    argp_failure (state, 2, err, "Couldn't get current login collection");
  return lid;
}

/* Add a specific process to be printed out.  */
static error_t
parse_pid (unsigned pid, struct argp_state *state)
{
  struct pids_argp_params *params = state->input;
  error_t err = add_pid (params->pids, params->num_pids, pid);
  if (err)
    argp_failure (state, 2, err, "%d: Cannot add process", pid);
  return err;
}

/* Print out all process from the given session.  */
static error_t
parse_sid (unsigned sid, struct argp_state *state)
{
  struct pids_argp_params *params = state->input;
  error_t err =
    add_fn_pids (params->pids, params->num_pids, sid, proc_getsessionpids);
  if (err)
    argp_failure (state, 2, err, "%d: Cannot add session", sid);
  return err;
}

/* Print out all process from the given login collection.  */
static error_t
parse_lid (unsigned lid, struct argp_state *state)
{
  struct pids_argp_params *params = state->input;
  error_t err =
    add_fn_pids (params->pids, params->num_pids, lid, proc_getloginpids);
  if (err)
    argp_failure (state, 2, err, "%d: Cannot add login collection", lid);
  return err;
}

/* Print out all process from the given process group.  */
static error_t
parse_pgrp (unsigned pgrp, struct argp_state *state)
{
  struct pids_argp_params *params = state->input;
  error_t err =
    add_fn_pids (params->pids, params->num_pids, pgrp, proc_getpgrppids);
  if (err)
    argp_failure (state, 2, err, "%d: Cannot add process group", pgrp);
  return err;
}

#define OA OPTION_ARG_OPTIONAL

/* Options for PIDS_ARGP.  */
static const struct argp_option options[] =
{
  {"login",      'L',     "LID", OA, "Processes from the login"
                                      " collection LID (which defaults that of"
                                      " the current process)"},
  {"lid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {"pid",        'p',     "PID",  0,  "The process PID"},
  {"pgrp",       'P',     "PGRP", 0,  "Processes in process group PGRP"},
  {"session",    'S',     "SID",  OA, "Processes from the session SID"
                                      " (which defaults to that of the"
                                      " current process)"},
  {"sid",        0,       0,      OPTION_ALIAS | OPTION_HIDDEN},
  {0, 0}
};

/* Parse one option/arg for PIDS_ARGP.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct pids_argp_params *params = state->input;

  switch (key)
    {
    case 'p':
      return
	parse_numlist (arg, parse_pid, NULL, NULL, "process id", state);
    case 'S':
      return
	parse_numlist (arg, parse_sid, current_sid, NULL, "session id", state);
    case 'L':
      return
	parse_numlist (arg, parse_lid, current_lid, NULL, "login collection",
		       state);
    case 'P':
      return
	parse_numlist (arg, parse_pgrp, NULL, NULL, "process group", state);

    case ARGP_KEY_ARG:
      if (params->parse_pid_args)
	return parse_numlist (arg, parse_pid, NULL, NULL, "process id", state);
      /* Else fall through */

    default:
      return ARGP_ERR_UNKNOWN;
    }
}

/* Filtering of help output strings for PIDS_ARGP.  */
static char *
help_filter (int key, const char *text, void *input)
{
  struct pids_argp_params *params = input;

  /* Describe the optional behavior of parsing normal args as pids.  */
  if (key == ARGP_KEY_HELP_ARGS_DOC && params->parse_pid_args)
    return strdup ("[PID...]");

  return (char *)text;
}

/* A parser for selecting a set of pids.  */
struct argp pids_argp = { options, parse_opt, 0, 0, 0, help_filter };
