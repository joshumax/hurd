/* Subtract one set of user and group ids from another

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

#include <errno.h>

#include "idvec.h"
#include "ugids.h"

/* Remove the gids in SUB from those in GIDS, except where they are implied
   in SUB (as represented by SUB_IMP), but not in GIDS (as represented by
   GIDS_IMP).  */
static
error_t _sub_gids (struct idvec *gids, struct idvec *gids_imp,
		   const struct idvec *sub, const struct idvec *sub_imp)
{
  error_t err;
  /* What we'll remove from GIDS.  */
  struct idvec delta = IDVEC_INIT;
  /* Those implied ids in SUB that we *won't* remove, because they're not
     also implied in GIDS.  */
  struct idvec delta_suppress = IDVEC_INIT;

  err = idvec_set (&delta, sub);
  if (! err)
    err = idvec_set (&delta_suppress, sub_imp);
  if (! err)
    {
      /* Don't suppress those implied ids that are implied in both. */
      idvec_subtract (&delta_suppress, gids_imp);
      idvec_subtract (&delta, &delta_suppress);

      /* Actually remove the gids.  */
      idvec_subtract (gids, &delta);
    }

  idvec_fini (&delta);
  idvec_fini (&delta_suppress);

  return err;
}

/* Remove the  in SUB from those in GIDS, except where they are implied
   in SUB (as represented by SUB_IMP), but not in GIDS (as represented by
   GIDS_IMP).  */
static
error_t _sub (struct idvec *uids, struct idvec *gids, struct idvec *gids_imp,
	      const struct idvec *sub_uids,
	      const struct idvec *sub_gids, const struct idvec *sub_gids_imp)
{
  error_t err;
  struct idvec new_uids = IDVEC_INIT; /* The set of uids after subtraction.  */
  struct idvec no_sub_gids = IDVEC_INIT; /* Gids we *don't* want to remove
					    from GIDS, despite what's in
					    SUB_GIDS.  */
  struct idvec new_sub_gids = IDVEC_INIT;
  struct idvec new_sub_gids_imp = IDVEC_INIT;

  err = idvec_set (&new_uids, uids);
  if (! err)
    err = idvec_set (&new_sub_gids, sub_gids);
  if (! err)
    err = idvec_set (&new_sub_gids_imp, sub_gids_imp);
  if (! err)
    {
      idvec_subtract (&new_uids, sub_uids);

      err = idvec_merge_implied_gids (&no_sub_gids, &new_uids);
      if (! err)
	{
	  /* NO_SUB_GIDS is the intersection of implied gids in GIDS,
	     implied gids in SUB_GIDS, and implied gids after the subtraction
	     of uids -- we don't want to remove those implied gids because we
	     can't be sure which uids implied them (as there will be
	     appropriately implicative uids left after the subtraction).  */
	  idvec_keep (&no_sub_gids, gids_imp);
	  idvec_keep (&no_sub_gids, sub_gids_imp);

	  /* Remove those gids we don't want to subtract.  */
	  idvec_subtract (&new_sub_gids, &no_sub_gids);
	  idvec_subtract (&new_sub_gids_imp, &no_sub_gids);

	  /* Do the group subtraction.  */
	  err = _sub_gids (gids, gids_imp, &new_sub_gids, &new_sub_gids_imp);
	  if (! err)
	    /* Finally, if no problems, do the uid subtraction.  */
	    err = idvec_set (uids, &new_uids);
	}
    }

  idvec_fini (&new_uids);
  idvec_fini (&no_sub_gids);
  idvec_fini (&new_sub_gids);
  idvec_fini (&new_sub_gids_imp);

  return err;
}

/* Remove the ids in SUB from those in UGIDS.  */
error_t
ugids_subtract (struct ugids *ugids, const struct ugids *sub)
{
  error_t err =
    _sub (&ugids->eff_uids, &ugids->eff_gids, &ugids->imp_eff_gids,
	  &sub->eff_uids, &sub->eff_gids, &sub->imp_eff_gids);

  if (! err)
    /* If this second call to _sub fails, ugids will be in an inconsistent
       state, but oh well.  */
    err = _sub (&ugids->avail_uids, &ugids->avail_gids, &ugids->imp_avail_gids,
		&sub->avail_uids, &sub->avail_gids, &sub->imp_avail_gids);

  return err;
}
