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
    ports_create_port (diskfs_protid_class, diskfs_port_bucket,
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
   the uid set is UID (length NUIDS); the gid set is GID (length NGIDS). */
void
diskfs_finish_protid (struct protid *cred, uid_t *uids, int nuids,
		      gid_t *gids, int ngids)
{
  if (!uids)
    nuids = 1;
  if (!gids)
    ngids = 1;

  cred->uids = malloc (nuids * sizeof (uid_t));
  cred->gids = malloc (ngids * sizeof (uid_t));
  cred->nuids = nuids;
  cred->ngids = ngids;

  if (uids)
    bcopy (uids, cred->uids, nuids * sizeof (uid_t));
  else
    *cred->uids = 0;

  if (gids)
    bcopy (gids, cred->gids, ngids * sizeof (uid_t));
  else
    *cred->gids = 0;
}

/* Create and return a protid for an existing peropen PO in CRED.  The uid
   set is UID (length NUIDS); the gid set is GID (length NGIDS).  The node
   PO->np must be locked. */
error_t
diskfs_create_protid (struct peropen *po, uid_t *uids, int nuids,
		      uid_t *gids, int ngids, struct protid **cred)
{
  error_t err = diskfs_start_protid (po, cred);
  if (! err)
    diskfs_finish_protid (*cred, uids, nuids, gids, ngids);
  return err;
}

/* Backward compatibility.  Use diskfs_create_protid. */
struct protid *
diskfs_make_protid (struct peropen *po, uid_t *uids, int nuids,
		    uid_t *gids, int ngids)
{
  struct protid *cred;
  if (diskfs_create_protid (po, uids, nuids, gids, ngids, &cred))
    cred = 0;
  return cred;
}

#include "linkwarn.h"
obsolete (diskfs_make_protid, diskfs_create_protid)
