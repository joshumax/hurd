/* Remove authentication from selected processes

   Copyright (C) 1997, 1998 Free Software Foundation, Inc.

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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hurd.h>
#include <argp.h>
#include <error.h>
#include <version.h>

#include "frobauth.h"

const char *argp_program_version = STANDARD_HURD_VERSION (rmauth);

static const struct argp_option options[] =
{
#ifndef UNSU
  {"save",   's', 0, 0, "Save removed effective ids as available ids"},
#endif
  { 0 }
};

#ifdef UNSU
static struct argp_child child_argps[] = {{ &frobauth_posix_argp }, { 0 }};
#else
static struct argp_child child_argps[] = {{ &frobauth_ea_argp }, { 0 }};
#endif

static char doc[] =
  "Remove user/group ids from the authentication of selected processes";

int
main (int argc, char *argv[])
{
  int save = 0;			/* save effective ids */
  struct frobauth frobauth = FROBAUTH_INIT;
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 's': save = 1; break;
	case ARGP_KEY_INIT:
	  state->child_inputs[0] = state->input; break;
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  /* Modify UGIDS, to be what PID's new authentication should be, UGIDS is
     what the user specified.  */
  error_t modify (struct ugids *ugids, const struct ugids *remove,
		  pid_t pid, void *hook)
    {
      error_t err = 0;
      struct ugids saved = UGIDS_INIT;

      if (save)
	ugids_set (&saved, ugids);

      err = ugids_subtract (ugids, remove);

      if (save)
	{
	  ugids_subtract (&saved, ugids);
	  ugids_save (&saved);
	  ugids_merge (ugids, &saved);
	}

      return err;
    }
  void print_info (const struct ugids *new,
		   const struct ugids *old,
		   const struct ugids *removed,
		   pid_t pid, void *hook)
    {
      char *delta_rep;
      struct ugids delta = UGIDS_INIT;

      ugids_set (&delta, old);
      ugids_subtract (&delta, new);

      delta_rep = ugids_rep (&delta, 1, 1, 0, 0, 0);
      printf ("%d: Removed %s\n", pid, delta_rep);

      free (delta_rep);
      ugids_fini (&delta);
    }
  struct argp argp = { options, parse_opt, 0, doc, child_argps };

#ifdef UNSU
  frobauth.default_user = 0;
#endif
  frobauth.require_ids = 1;

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, &frobauth);

  if (frobauth_modify (&frobauth, 0, 0, modify, print_info, 0))
    return 0;
  else
    return 1;
}
