/* Set posix-compatible ugids

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

#include "ugids.h"

/* Install UID into UGIDS as the main user, making sure that the posix
   `real' and `saved' uid slots are filled in, and similarly add all
   groups to which UID belongs.  */
error_t
ugids_set_posix_user (struct ugids *ugids, uid_t uid)
{
  error_t err;
  struct idvec imp_gids = IDVEC_INIT;
  uid_t uids_ids[] = { uid };
  struct idvec uids = { uids_ids, 1 };

  error_t update_real (struct idvec *avail_ids, uid_t id)
    {
      if (avail_ids->num == 0
	  || !idvec_tail_contains (avail_ids, 1, avail_ids->ids[0]))
	return idvec_insert (avail_ids, 0, id);
      else
	avail_ids->ids[0] = id;
      return 0;
    }

  idvec_merge_implied_gids (&imp_gids, &uids);

  /* Try to add UID.  */
  err = idvec_insert_only (&ugids->eff_uids, 0, uid); /* Effective */
  if (! err)
    err = update_real (&ugids->avail_uids, uid); /* Real */
  if (! err)
    err = idvec_insert_only (&ugids->avail_uids, 1, uid); /* Saved */

  if (!err && imp_gids.num > 0)
    /* Now do the gids.  */
    {
      /* The main gid associated with UID (usually from /etc/passwd).  */
      gid_t gid = imp_gids.ids[0];
      /* True if GID was already an available gid.  */
      int gid_was_avail = idvec_contains (&ugids->avail_gids, gid);

      /* Update the implied sets for the gids: they're implied unless
	 they were present as non-implied gids before.  Here we
	 remove existing effective gids from the IMP_GIDS before we
	 added it to the implied sets -- if some of those gids were
	 actually implied, they'll already be present in the implied
	 set. */
      idvec_subtract (&imp_gids, &ugids->eff_gids);

      /* Now add GID, as effective, real, and saved gids.  */
      if (! err)	/* Effective */
	err = idvec_insert_only (&ugids->eff_gids, 0, gid);
      if (! err)	/* Real */
	err = update_real (&ugids->avail_gids, gid);
      if (! err)	/* Saved */
	err = idvec_insert_only (&ugids->avail_gids, 1, gid);

      /* Mark GID as implied in the available gids unless it was already
	 present (in which case its implied status is already settled).  */
      if (!err && !gid_was_avail)
	err = idvec_add (&ugids->imp_avail_gids, gid);

      /* Add the other implied gids to the end of the effective gids.  */
      if (! err)
	err = idvec_merge (&ugids->eff_gids, &imp_gids);
      /* And make them implied.  */
      if (! err)
	err = idvec_merge (&ugids->imp_eff_gids, &imp_gids);
    }

  idvec_fini (&imp_gids);

  return err;
}
