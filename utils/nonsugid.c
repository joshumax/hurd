/* Get our ids, minus any setuid result

   Copyright (C) 1995,96,97,2000 Free Software Foundation, Inc.
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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <errno.h>
#include <idvec.h>
#include <hurd.h>

/* Make sure that the [UG]IDS are filled in.  To make them useful for
   su'ing, each is the avail ids with the saved set-ID removed, and all
   effective ids but the first appended; this gets rid of the effect of
   being suid, and is useful as a new process's avail id list (e.g., the
   real id is right).  */
error_t
get_nonsugid_ids (struct idvec *uids, struct idvec *gids)
{
  if (uids->num == 0 && gids->num == 0)
    {
      error_t err = 0;
      static auth_t auth = MACH_PORT_NULL;
      struct idvec *p_eff_uids = make_idvec ();
      struct idvec *p_eff_gids = make_idvec ();

      if (!p_eff_uids || !p_eff_gids)
	err = ENOMEM;

      if (auth == MACH_PORT_NULL)
	auth = getauth ();

      if (! err)
	err = idvec_merge_auth (p_eff_uids, uids, p_eff_gids, gids, auth);
      if (! err)
	{
	  idvec_delete (p_eff_uids, 0); /* Remove effective ID from setuid.  */
	  idvec_delete (p_eff_gids, 0);
	  idvec_delete (uids, 1); /* Remove saved set-ID from setuid.  */
	  idvec_delete (gids, 1);
	  if (! err)
	    err = idvec_merge (uids, p_eff_uids);
	  if (! err)
	    err = idvec_merge (gids, p_eff_gids);
	}

      return err;
    }
  else
    return 0;
}
