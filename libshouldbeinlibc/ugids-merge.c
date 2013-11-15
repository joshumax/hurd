/* Merging of ugids

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

static error_t
_merge_gids (struct idvec *gids, struct idvec *gids_imp,
	     const struct idvec *new, const struct idvec *new_imp)
{
  error_t err;
  /* Gids that exist in both GIDS and NEW can only be implied in the result
     if they are implied in both; here GIDS_STRONG and NEW_STRONG contain
     those gids which shouldn't be implied in the result because they are not
     in either of the sources.  */
  struct idvec gids_strong = IDVEC_INIT;
  struct idvec new_strong = IDVEC_INIT;

  err = idvec_set (&gids_strong, gids);
  if (! err)
    err = idvec_set (&new_strong, new);
  if (! err)
    {
      idvec_subtract (&gids_strong, gids_imp);
      idvec_subtract (&new_strong, new_imp);

      err = idvec_merge (gids, new);
      if (! err)
	{
	  err = idvec_merge (gids_imp, new_imp);
	  if (! err)
	    {
	      idvec_subtract (gids_imp, &gids_strong);
	      idvec_subtract (gids_imp, &new_strong);
	    }
	}
    }

  idvec_fini (&gids_strong);
  idvec_fini (&new_strong);

  return err;
}

/* Add all ids in NEW to UGIDS.  */
error_t
ugids_merge (struct ugids *ugids, const struct ugids *new)
{
  error_t err;
  err = idvec_merge (&ugids->eff_uids, &new->eff_uids);
  if (! err)
    err = idvec_merge (&ugids->avail_uids, &new->avail_uids);
  if (! err)
    err = _merge_gids (&ugids->eff_gids, &ugids->imp_eff_gids,
		       &new->eff_gids, &new->imp_eff_gids);
  if (! err)
    err = _merge_gids (&ugids->avail_gids, &ugids->imp_avail_gids,
		       &new->avail_gids, &new->imp_avail_gids);
  return err;
}

/* Set the ids in UGIDS to those in NEW.  */
error_t
ugids_set (struct ugids *ugids, const struct ugids *new)
{
  idvec_clear (&ugids->eff_uids);
  idvec_clear (&ugids->eff_gids);
  idvec_clear (&ugids->avail_uids);
  idvec_clear (&ugids->avail_gids);
  idvec_clear (&ugids->imp_eff_gids);
  idvec_clear (&ugids->imp_avail_gids);
  return ugids_merge (ugids, new);
}

/* Save any effective ids in UGIDS by merging them into the available ids,
   and removing them from the effective ones.  */
error_t 
ugids_save (struct ugids *ugids)
{
  error_t err = idvec_merge (&ugids->avail_uids, &ugids->eff_uids);
  if (! err)
    err = _merge_gids (&ugids->avail_gids, &ugids->imp_avail_gids,
		       &ugids->eff_gids, &ugids->imp_eff_gids);
  if (! err)
    {
      idvec_clear (&ugids->eff_uids);
      idvec_clear (&ugids->eff_gids);
      idvec_clear (&ugids->imp_eff_gids);
    }
  return err;
}
