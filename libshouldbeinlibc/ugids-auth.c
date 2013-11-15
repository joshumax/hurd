/* Translate user and group ids to/from auth ports

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

#include <hurd.h>

#include "idvec.h"
#include "ugids.h"

/* Make an auth port from UGIDS and return it in AUTH, using authority in
   both the auth port FROM and the current auth port.  */
error_t
ugids_make_auth (const struct ugids *ugids,
		 const auth_t *from, size_t num_from,
		 auth_t *auth)
{
  auth_t cur_auth = getauth ();
  error_t err =
    auth_makeauth (cur_auth, (auth_t *)from, MACH_MSG_TYPE_COPY_SEND, num_from,
		   ugids->eff_uids.ids, ugids->eff_uids.num,
		   ugids->avail_uids.ids, ugids->avail_uids.num,
		   ugids->eff_gids.ids, ugids->eff_gids.num,
		   ugids->avail_gids.ids, ugids->avail_gids.num,
		   auth);
  mach_port_deallocate (mach_task_self (), cur_auth);
  return err;
}

/* Merge the ids from the auth port  AUTH into UGIDS.  */
error_t
ugids_merge_auth (struct ugids *ugids, auth_t auth)
{
  return
    idvec_merge_auth (&ugids->eff_uids, &ugids->avail_uids,
		      &ugids->eff_gids, &ugids->avail_gids,
		      auth);
}
