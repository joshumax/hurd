/* Frob user and group ids

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

#include "idvec.h"
#include "ugids.h"

/* Return a new ugids structure, or 0 if an allocation error occurs.  */
struct ugids *
make_ugids ()
{
  struct ugids *u = malloc (sizeof (struct ugids));
  if (u)
    memset (u, 0, sizeof *u);
  return u;
}

/* Add a new uid to UGIDS.  If AVAIL is true, it's added to the avail uids
   instead of the effective ones.  */
error_t
ugids_add_uid (struct ugids *ugids, uid_t uid, int avail)
{
  return idvec_add_new (avail ? &ugids->avail_uids : &ugids->eff_uids, uid);
}

/* Add a new gid to UGIDS.  If AVAIL is true, it's added to the avail gids
   instead of the effective ones.  */
error_t
ugids_add_gid (struct ugids *ugids, gid_t gid, int avail)
{
  error_t err =
    idvec_add_new (avail ? &ugids->avail_gids : &ugids->eff_gids, gid);
  if (! err)
    /* Since this gid is now explicit, remove it from the appropriate implied
       set. */
    idvec_remove (avail ? &ugids->imp_avail_gids : &ugids->imp_eff_gids,
		  0, gid);
  return err;
}

/* Add UID to UGIDS, plus any gids to which that user belongs.  If AVAIL is
   true, they are added to the avail gids instead of the effective ones.  */
error_t
ugids_add_user (struct ugids *ugids, uid_t uid, int avail)
{
  error_t err;
  struct idvec imp_gids = IDVEC_INIT;
  uid_t uids_ids[] = { uid };
  struct idvec uids = { uids_ids, 1 };
  struct idvec *gids = avail ? &ugids->avail_gids : &ugids->eff_gids;

  idvec_merge_implied_gids (&imp_gids, &uids);

  /* Now remove any gids we already know about from IMP_GIDS.  For gids
     that weren't in the appropriate implied set before, this will
     ensure that they remain out after we merge IMP_GIDS into it, and
     ones that *were*, they will remain so.  */
  idvec_subtract (&imp_gids, gids);

  /* Try to add UID.  */
  err = idvec_add_new (avail ? &ugids->avail_uids : &ugids->eff_uids, uid);

  if (! err)
    /* Now that we've added UID, we can add appropriate implied gids.
       [If this fails, UGIDS will be an inconsistent state, but things
       are probably fucked anyhow] */
    err =
      idvec_merge (avail ? &ugids->avail_gids : &ugids->eff_gids,
		   &imp_gids);
  if (! err)
    err = idvec_merge ((avail
			? &ugids->imp_avail_gids
			: &ugids->imp_eff_gids),
		       &imp_gids);

  idvec_fini (&imp_gids);

  return err;
}
