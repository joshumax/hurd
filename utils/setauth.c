/* Change the authentication of selected processes

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

const char *argp_program_version = STANDARD_HURD_VERSION (setauth);

#define OPT_NO_SAVE 1

static const struct argp_option options[] =
{
#ifdef SU
  {"no-save", OPT_NO_SAVE, 0, 0, "Don't save removed effective ids as available ids"},
#else
  {"save",   's', 0, 0, "Save removed effective ids as available ids"},
#endif
  {"keep",   'k', 0, 0, "Keep old ids in addition to the new ones"},
  { 0 }
};

static struct argp_child child_argps[] = {{ &frobauth_posix_argp }, { 0 }};

static char doc[] =
  "Change the authentication of selected processes";

extern error_t
get_nonsugid_ids (struct idvec *uids, struct idvec *gids);

int
main (int argc, char *argv[])
{
  error_t err;
  auth_t auth;			/* Authority to make changes.  */
  int save = 0, keep = 0;
  struct idvec have_uids = IDVEC_INIT, have_gids = IDVEC_INIT;
  struct frobauth frobauth = FROBAUTH_INIT;

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 's': save = 1; break;
	case 'k': keep = 1; break;
	case OPT_NO_SAVE: save = 0; break;
	case ARGP_KEY_INIT:
	  state->child_inputs[0] = state->input; break;
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  /* Modify UGIDS, to be what PID's new authentication should be, UGIDS is
     what the user specified.  */
  error_t modify (struct ugids *ugids, const struct ugids *new,
		  pid_t pid, void *hook)
    {
      struct ugids old = UGIDS_INIT;
      ugids_set (&old, ugids);

      ugids_set (ugids, new);

      if (keep)
	ugids_merge (ugids, &old);
      if (save)
	{
	  ugids_save (&old);
	  ugids_merge (ugids, &old);
	}

      return 0;
    }
  void print_info (const struct ugids *new,
		   const struct ugids *old,
		   const struct ugids *user,
		   pid_t pid, void *hook)
    {
      char *new_rep = ugids_rep (new, 1, 1, 0, 0, 0);
      printf ("%d: Changed auth to %s\n", pid, new_rep);
      free (new_rep);
    }
  struct argp argp = { options, parse_opt, 0, doc, child_argps };

#ifdef SU
  frobauth.default_user = 0;
  save = 1;			/* Default to saving ids */
#endif

  /* Parse our command line.  This shouldn't ever return an error.  */
  argp_parse (&argp, argc, argv, 0, 0, &frobauth);

  /* See what the invoking user is authorized to do.  */
  err = get_nonsugid_ids (&have_uids, &have_gids);
  if (err)
    error (52, err, "Cannot get invoking authentication");

  /* Check passwords.  */
  err = ugids_verify_make_auth (&frobauth.ugids, &have_uids, &have_gids, 0, 0,
				0, 0, &auth);
  if (err == EACCES)
    error (15, 0, "Invalid password");
  else if (err)
    error (16, err, "Authentication failure");

  if (frobauth_modify (&frobauth, &auth, 1, modify, print_info, 0))
    return 0;
  else
    return 1;
}
