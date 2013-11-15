/* Calculate implied group ids from user ids

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

/* Mark as implied all gids in UGIDS that can be implied from its uids.  */
error_t
ugids_imply_all (struct ugids *ugids)
{
  error_t err;
  err = idvec_merge_implied_gids (&ugids->imp_eff_gids, &ugids->eff_uids);
  if (! err)
    err =
      idvec_merge_implied_gids (&ugids->imp_avail_gids, &ugids->avail_uids);
  return err;
}
