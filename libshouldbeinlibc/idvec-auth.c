/* Idvec functions that interact with an auth server

   Copyright (C) 1995, 1998, 1999, 2001, 2002, 2008
     Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <mach.h>
#include <sys/mman.h>
#include <hurd/auth.h>
#include <errno.h>

#include "idvec.h"

/* Add to all of EFF_UIDS, AVAIL_UIDS, EFF_GIDS, AVAIL_GIDS (as if with
   idvec_merge_ids()) the ids associated with the auth port AUTH.  Any of
   these parameters may be NULL if that information isn't desired.  */
error_t
idvec_merge_auth (struct idvec *eff_uids, struct idvec *avail_uids,
		  struct idvec *eff_gids, struct idvec *avail_gids,
		  auth_t auth)
{
  error_t err;
  uid_t eff_uid_buf[10], avail_uid_buf[20];
  uid_t *_eff_uids = eff_uid_buf, *_avail_uids = avail_uid_buf;
  size_t num_eff_uids = 10, num_avail_uids = 20;
  uid_t eff_gid_buf[10], avail_gid_buf[20];
  uid_t *_eff_gids = eff_gid_buf, *_avail_gids = avail_gid_buf;
  size_t num_eff_gids = 10, num_avail_gids = 20;

  err = auth_getids (auth,
		     &_eff_uids, &num_eff_uids, &_avail_uids, &num_avail_uids,
		     &_eff_gids, &num_eff_gids, &_avail_gids, &num_avail_gids);
  if (err)
    return err;

  if (eff_uids)
    err = idvec_grow (eff_uids, num_eff_uids);
  if (avail_uids && !err)
    err = idvec_grow (avail_uids, num_avail_uids);
  if (eff_gids && !err)
    err = idvec_grow (eff_gids, num_eff_gids);
  if (avail_gids && !err)
    err = idvec_grow (avail_gids, num_avail_gids);

  if (!err)
    /* Now that we've ensured there's enough space, none of these should
       return an error.  */
    {
      if (eff_uids)
	idvec_merge_ids (eff_uids, _eff_uids, num_eff_uids);
      if (avail_uids)
	idvec_merge_ids (avail_uids, _avail_uids, num_avail_uids);
      if (eff_gids)
	idvec_merge_ids (eff_gids, _eff_gids, num_eff_gids);
      if (avail_gids)
	idvec_merge_ids (avail_gids, _avail_gids, num_avail_gids);
    }

  /* Deallocate any out-of-line memory we got back.  */
  if (_eff_uids != eff_uid_buf)
    munmap ((caddr_t) _eff_uids, num_eff_uids * sizeof (uid_t));
  if (_avail_uids != avail_uid_buf)
    munmap ((caddr_t) _avail_uids, num_avail_uids * sizeof (uid_t));
  if (_eff_gids != eff_gid_buf)
    munmap ((caddr_t) _eff_gids, num_eff_gids * sizeof (gid_t));
  if (_avail_gids != avail_gid_buf)
    munmap ((caddr_t) _avail_gids, num_avail_gids * sizeof (gid_t));

  return err;
}
