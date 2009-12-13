/* Setuid reauthentication for exec

   Copyright (C) 1995,96,97,2002 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>,
     from the original by Michael I. Bushnell p/BSG  <mib@gnu.org>

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

#include <hurd/io.h>
#include <hurd/process.h>
#include <hurd/auth.h>
#include <idvec.h>

#include "fshelp.h"

extern error_t
exec_reauth (auth_t auth, int secure, int must_reauth,
	     mach_port_t *ports, unsigned num_ports,
	     mach_port_t *fds, unsigned num_fds);

/* If SUID or SGID is true, adds UID and/or GID respectively to the
   authentication in PORTS[INIT_PORT_AUTH], and replaces it with the result.
   All the other ports in PORTS and FDS are then reauthenticated, using any
   privileges available through AUTH.  If GET_FILE_IDS is non-NULL, and the
   auth port in PORTS[INIT_PORT_AUTH] is bogus, it is called to get a list of
   uids and gids from the file to use as a replacement.  If SECURE is
   non-NULL, whether not the added ids are new is returned in it.  If either
   the uid or gid case fails, then the other may still be applied.  */
error_t
fshelp_exec_reauth (int suid, uid_t uid, int sgid, gid_t gid,
		    auth_t auth,
		    error_t
		      (*get_file_ids)(struct idvec *uids, struct idvec *gids),
		    mach_port_t *ports, mach_msg_type_number_t num_ports,
		    mach_port_t *fds, mach_msg_type_number_t num_fds,
		    int *secure)
{
  error_t err = 0;
  int _secure = 0;

  if (suid || sgid)
    {
      int already_root = 0;
      auth_t newauth;
      /* These variables describe the auth port that the user gave us. */
      struct idvec *eff_uids = make_idvec (), *avail_uids = make_idvec ();
      struct idvec *eff_gids = make_idvec (), *avail_gids = make_idvec ();

      if (!eff_uids || !avail_uids || !eff_gids || !avail_gids)
	goto abandon_suid;	/* Allocation error; probably toast, but... */

      /* STEP 0: Fetch the user's current id's. */
      err = idvec_merge_auth (eff_uids, avail_uids, eff_gids, avail_gids,
			      ports[INIT_PORT_AUTH]);
      if (err)
	goto abandon_suid;

      already_root =
	idvec_contains (eff_uids, 0) || idvec_contains (avail_uids, 0);

      /* If the user's auth port is fraudulent, then these values will be
	 wrong.  No matter; we will repeat these checks using secure id sets
	 later if the port turns out to be bogus.  */
      if (suid)
	err = idvec_setid (eff_uids, avail_uids, uid, &_secure);
      if (sgid && !err)
	err = idvec_setid (eff_gids, avail_gids, gid, &_secure);
      if (err)
	goto abandon_suid;

      /* STEP 3: Attempt to create this new auth handle. */
      err = auth_makeauth (auth, &ports[INIT_PORT_AUTH],
			   MACH_MSG_TYPE_COPY_SEND, 1,
			   eff_uids->ids, eff_uids->num,
			   avail_uids->ids, avail_uids->num,
			   eff_gids->ids, eff_gids->num,
			   avail_gids->ids, avail_gids->num,
			   &newauth);
      if (err == EINVAL && get_file_ids)
	/* The user's auth port was bogus.  As we can't trust what the user
	   has told us about ids, we use the authentication on the file being
	   execed (which we know is good), as the effective ids, and assume
	   no aux ids.  */
	{
	  /* Get rid of all ids from the bogus auth port.  */
	  idvec_clear (eff_uids);
	  idvec_clear (avail_uids);
	  idvec_clear (eff_gids);
	  idvec_clear (avail_gids);

	  /* Now add some from a source we trust.  */
	  err = (*get_file_ids)(eff_uids, eff_gids);

	  already_root = idvec_contains (eff_uids, 0);
	  if (suid && !err)
	    err = idvec_setid (eff_uids, avail_uids, uid, &_secure);
	  if (sgid && !err)
	    err = idvec_setid (eff_gids, avail_gids, gid, &_secure);
	  if (err)
	    goto abandon_suid;

	  /* Trrrry again...  */
	  err = auth_makeauth (auth, 0, MACH_MSG_TYPE_COPY_SEND, 1,
			       eff_uids->ids, eff_uids->num,
			       avail_uids->ids, avail_uids->num,
			       eff_gids->ids, eff_gids->num,
			       avail_gids->ids, avail_gids->num,
			       &newauth);
	}

      if (err)
	goto abandon_suid;

      if (already_root)
	_secure = 0;		/* executive privilege */

      /* Re-authenticate the exec parameters.  */
      exec_reauth (newauth, _secure, 0, ports, num_ports, fds, num_fds);

      proc_setowner (ports[INIT_PORT_PROC],
		     eff_uids->num > 0 ? eff_uids->ids[0] : 0,
		     !eff_uids->num);

    abandon_suid:
      if (eff_uids)
	idvec_free (eff_uids);
      if (avail_uids)
	idvec_free (avail_uids);
      if (eff_gids)
	idvec_free (eff_gids);
      if (avail_gids)
	idvec_free (avail_gids);
    }

  if (secure)
    *secure = _secure;

  return err;
}
