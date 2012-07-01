/* iohelp_restrict_iouser -- helper for io_restrict_auth implementations
   Copyright (C) 2002 Free Software Foundation

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

#include "iohelp.h"

/* Tell if the array LIST (of size N) contains a member equal to QUERY. */
static inline int
listmember (const uid_t *list, uid_t query, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (list[i] == query)
      return 1;
  return 0;
}

error_t
iohelp_restrict_iouser (struct iouser **new_user,
			const struct iouser *old_user,
			const uid_t *uids, int nuids,
			const gid_t *gids, int ngids)
{
  if (idvec_contains (old_user->uids, 0))
    /* OLD_USER has root access, and so may use any ids.  */
    return iohelp_create_complex_iouser (new_user, uids, nuids, gids, ngids);
  else
    {
      struct idvec *uvec, *gvec;
      unsigned int i;
      error_t err;

      uvec = make_idvec ();
      if (! uvec)
        return ENOMEM;

      gvec = make_idvec ();
      if (! gvec)
        {
	  idvec_free (uvec);
	  return ENOMEM;
	}

      /* Otherwise, use any of the requested ids that OLD_USER already has.  */
      for (i = 0; i < old_user->uids->num; i++)
	if (listmember (uids, old_user->uids->ids[i], nuids))
	  {
	    err = idvec_add (uvec, old_user->uids->ids[i]);
	    if (err)
	      goto out;
	  }
      for (i = 0; i < old_user->gids->num; i++)
	if (listmember (gids, old_user->gids->ids[i], ngids))
	  {
	    err = idvec_add (gvec, old_user->gids->ids[i]);
	    if (err)
	      goto out;
	  }

      err = iohelp_create_iouser (new_user, uvec, gvec);

      if (err)
        {
        out:
	  idvec_free (uvec);
	  idvec_free (gvec);
	}
      return err;
    }
}
