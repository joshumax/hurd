/* Common interface for auth frobbing utilities

   Copyright (C) 1997 Free Software Foundation, Inc.

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

/* This file is rather a mess of intertwined argps; it shoud be redone as a
   single level once argp can handle dynamic option frobbing.  XXX */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <hurd.h>
#include <hurd/process.h>

#include "frobauth.h"
#include "ugids.h"
#include "pids.h"

static struct argp common_argp;
static struct argp fr_ugids_argp;

static const struct argp_option common_options[] =
{
  {"verbose",   'v', 0, 0, "Print informational messages"},
  {"dry-run",   'n', 0, 0, "Don't do anything, just print what would be done"},
  { 0 }
};
static struct argp_child common_child_argps[] =
{
  { &pids_argp, 0, "Process selection:" },
  { 0 }
};

static const char common_args_doc[] = "USER...";
static const char common_doc[] =
  "\vBy default, all processes in the current login collection are selected";

static struct argp_child ugids_child_argps[] =
{
  { &ugids_argp, 0, "User/group ids:" },
  { 0 }
};

/* An argp on top of the base frobauth argp that provides switchable
   effective/available ids (XXX this junk should be moved into a single argp
   [indeed, into ugids_argp] once argp can deal with the init routine
   frobbing the argp source).  */
static const struct argp_option ea_options[] =
{
  {"available", 'a', 0, 0, "USER... specifies available ids"},
  {"effective", 'e', 0, 0, "USER... specifies effective ids"},
  { 0 }
};

static struct argp_child ea_posix_child_argps[] =
{
  { &common_argp },
  { &fr_ugids_argp },
  { 0 }
};

static struct argp_child no_ugids_child_argps[] =
{
  { &common_argp },
  { 0 }
};

/* This holds state information that's only active during parsing.  */
struct frobauth_argp_state
{
  struct frobauth *frobauth;
  struct pids_argp_params pids_argp_params;
  struct ugids_argp_params ugids_argp_params;
};

static error_t
common_parse_opt (int key, char *arg, struct argp_state *state)
{
  struct frobauth_argp_state *fs = state->input;
  struct frobauth *frobauth = fs->frobauth;

  switch (key)
    {
    case 'v':
      frobauth->verbose = 1; break;
    case 'n':
      frobauth->dry_run = 1; break;

    case ARGP_KEY_END:
      if (frobauth->num_pids == 0)
	/* No pids specified!  By default, do the current login collection.  */
	{
	  pid_t lid;
	  error_t err = proc_getloginid (getproc (), getpid (), &lid);

	  if (err)
	    argp_failure (state, 2, err,
			  "Couldn't get current login collection");

	  err = add_fn_pids (&frobauth->pids, &frobauth->num_pids,
			     lid, proc_getloginpids);
	  if (err)
	    argp_failure (state, 3, err,
			  "%d: Couldn't get login collection pids", lid);

	  return err;
	}
      break;

    case ARGP_KEY_INIT:
      memset (fs, 0, sizeof *fs);
      fs->frobauth = frobauth;
      fs->pids_argp_params.pids = &frobauth->pids;
      fs->pids_argp_params.num_pids = &frobauth->num_pids;
      state->child_inputs[0] = &fs->pids_argp_params;
      break;

    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_ERROR:
      free (fs);

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
ugids_parse_opt (int key, char *arg, struct argp_state *state)
{
  struct frobauth_argp_state *fs = state->input;
  struct frobauth *frobauth = fs->frobauth;

  switch (key)
    {
    case ARGP_KEY_INIT:
      fs->ugids_argp_params.ugids = &frobauth->ugids;
      fs->ugids_argp_params.parse_user_args = 1;
      fs->ugids_argp_params.default_user = frobauth->default_user;
      fs->ugids_argp_params.require_ids = frobauth->require_ids;
      fs->pids_argp_params.pids = &frobauth->pids;
      fs->pids_argp_params.num_pids = &frobauth->num_pids;

      state->child_inputs[0] = &fs->ugids_argp_params;

      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
ea_parse_opt (int key, char *arg, struct argp_state *state)
{
  struct frobauth_argp_state *fs = state->hook;

  switch (key)
    {
    case 'a':
      fs->ugids_argp_params.user_args_are_available = 1;
      break;
    case 'e':
      fs->ugids_argp_params.user_args_are_effective = 1;
      break;

    case ARGP_KEY_ARG:
      if (!fs->ugids_argp_params.user_args_are_effective
	  && !fs->ugids_argp_params.user_args_are_available)
	/* Default to effective.  */
	fs->ugids_argp_params.user_args_are_effective = 1;

      /* Let someone else parse the arg.  */
      return ARGP_ERR_UNKNOWN;

    case ARGP_KEY_INIT:
      /* Initialize inputs for child parsers.  */
      fs = state->hook = malloc (sizeof (struct frobauth_argp_state));
      if (! fs)
	return ENOMEM;

      fs->frobauth = state->input;
      state->child_inputs[0] = fs; /* Pass our state to the common parser.  */
      state->child_inputs[1] = fs; /* Pass our state to the common parser.  */
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
posix_parse_opt (int key, char *arg, struct argp_state *state)
{
  struct frobauth_argp_state *fs;

  switch (key)
    {
    case ARGP_KEY_INIT:
      /* Initialize inputs for child parsers.  */
      fs = state->hook = malloc (sizeof (struct frobauth_argp_state));
      if (! fs)
	return ENOMEM;

      fs->frobauth = state->input;
      state->child_inputs[0] = fs; /* Pass our state to the common parser.  */
      state->child_inputs[1] = fs; /* Pass our state to the common parser.  */
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
no_ugids_parse_opt (int key, char *arg, struct argp_state *state)
{
  struct frobauth_argp_state *fs;

  switch (key)
    {
    case ARGP_KEY_INIT:
      /* Initialize inputs for child parsers.  */
      fs = state->hook = malloc (sizeof (struct frobauth_argp_state));
      if (! fs)
	return ENOMEM;

      fs->frobauth = state->input;
      state->child_inputs[0] = fs; /* Pass our state to the common parser.  */
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp common_argp =
{
  common_options, common_parse_opt, 0, common_doc, common_child_argps
};
static struct argp fr_ugids_argp =
{
  0, ugids_parse_opt, 0, 0, ugids_child_argps
};

/* Parse frobauth args/options, where user args are added as single ids to
   either the effective or available ids.  */
struct argp frobauth_ea_argp =
{
  ea_options, ea_parse_opt, 0, 0, ea_posix_child_argps
};

/* Parse frobauth args/options, where user args are added as posix user.  */
struct argp frobauth_posix_argp =
{
  0, posix_parse_opt, 0, 0, ea_posix_child_argps
};

/* Parse frobauth args/options, where user args are added as posix user.  */
struct argp frobauth_no_ugids_argp =
{
  0, no_ugids_parse_opt, 0, 0, no_ugids_child_argps
};
