/*
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

#include "priv.h"
#include <string.h>

/* Build and return in CRED a protid which has no user identification, for
   peropen PO.  The node PO->np must be locked.  */
error_t
diskfs_start_protid (struct peropen *po, struct protid **cred)
{
  error_t err =
    ports_create_port_noinstall (diskfs_protid_class, diskfs_port_bucket,
				 sizeof (struct protid), cred);
  if (! err)
    {
      po->refcnt++;
      (*cred)->po = po;
      (*cred)->shared_object = MACH_PORT_NULL;
      (*cred)->mapped = 0;
    }
  return err;
}

/* Finish building protid CRED started with diskfs_start_protid;
   the user to install is USER. */
void
diskfs_finish_protid (struct protid *cred, struct iouser *user)
{
  if (!user)
    {
      uid_t zero = 0;
      /* Create one for root */
      user = iohelp_create_iouser (make_idvec (), make_idvec ());
      idvec_set_ids (user->uids, &zero, 1);
      idvec_set_ids (user->gids, &zero, 1);
      cred->user = user;
    }
  else
    cred->user = iohelp_dup_iouser (user);

  mach_port_move_member (mach_task_self (), cred->pi.port_right, 
			 diskfs_port_bucket->portset);
}

/* Create and return a protid for an existing peropen PO in CRED for USER.
   The node PO->np must be locked. */
error_t
diskfs_create_protid (struct peropen *po, struct iouser *user,
		      struct protid **cred)
{
  error_t err = diskfs_start_protid (po, cred);
  if (! err)
    diskfs_finish_protid (*cred, user);
  return err;
}


