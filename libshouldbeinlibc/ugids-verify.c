/* Verify user/group passwords

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

#include <stdlib.h>
#include <string.h>
#include <hurd.h>
#include <ctype.h>
#include <unistd.h>
#include <argp.h>
#include <pwd.h>
#include <grp.h>

#include "ugids.h"

/* Verify that we have the right to the ids in UGIDS, given that we already
   possess those in HAVE_UIDS and HAVE_GIDS, asking for passwords (with
   GETPASS, which defaults to the standard libc function getpass) where
   necessary.  0 is returned if access should be allowed, otherwise
   EINVAL if an incorrect password was entered, or an error relating to
   resource failure.  */
error_t
ugids_verify (const struct ugids *ugids,
	      const struct idvec *have_uids, const struct idvec *have_gids,
	      char *(*getpass_fn)(const char *prompt))
{
  error_t err;
  struct idvec check_uids = IDVEC_INIT; /* User-ids to verify.  */
  struct idvec check_gids = IDVEC_INIT; /* group-ids to verify.  */

  err = idvec_merge (&check_uids, &ugids->eff_uids);
  if (! err)
    err = idvec_merge (&check_uids, &ugids->avail_uids);
  if (! err)
    err = idvec_merge (&check_gids, &ugids->eff_gids);
  if (! err)
    err = idvec_merge (&check_gids, &ugids->avail_gids);

  if (! err)
    err = idvec_verify (&check_uids, &check_gids, have_uids, have_gids,
			getpass_fn);

  idvec_fini (&check_uids);
  idvec_fini (&check_gids);

  return err;
}
