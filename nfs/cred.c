/* Credential manipulation for NFS client
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <hurd/netfs.h>
#include <string.h>

#include "nfs.h"

/* This lock must always be held when manipulating the reference count
   on credential structures. */
static spin_lock_t cred_refcnt_lock = SPIN_LOCK_INITIALIZER;

/* Interpret CRED, returning newly malloced storage describing the
   user identification references in UIDS, NUIDS, GIDS, and NGIDS.
   See <hurd/libnetfs.h> for details. */
void
netfs_interpret_credential (struct netcred *cred, uid_t **uids,
			    int *nuids, uid_t **gids, int *ngids)
{
  /* Who says C isn't APL? */
  bcopy (cred->uids, *uids = malloc ((*nuids = cred->nuids) * sizeof (uid_t)),
	 cred->nuids * sizeof (uid_t));
  bcopy (cred->gids, *gids = malloc ((*ngids = cred->ngids) * sizeof (uid_t)),
	 cred->ngids * sizeof (uid_t));
}

/* Return a new reference to CRED.  See <hurd/libnetfs.h> for details. */
struct netcred *
netfs_copy_credential (struct netcred *cred)
{
  spin_lock (&cred_refcnt_lock);
  cred->refcnt++;
  spin_unlock (&cred_refcnt_lock);
  return cred;
}

/* Drop a reference to CRED.  See <hurd/libnetfs.h> for details. */
void
netfs_drop_credential (struct netcred *cred)
{
  spin_lock (&cred_refcnt_lock);
  cred->refcnt--;
  
  if (!cred->refcnt)
    {
      spin_unlock (&cred_refcnt_lock);
      free (cred);
    }
  else
    spin_unlock (&cred_refcnt_lock);
}

/* Make and return a new credential referring to the user identified
   by UIDS, NUIDS, GIDS, and NGIDS.  See <hurd/libnetfs.h> for
   details. */
struct netcred *
netfs_make_credential (uid_t *uids, 
		       int nuids, 
		       uid_t *gids,
		       int ngids)
{
  struct netcred *cred;
  
  cred = malloc (sizeof (struct netcred)
		 + nuids * sizeof (uid_t)
		 + ngids * sizeof (uid_t));
  cred->uids = (void *) cred + sizeof (struct netcred);
  cred->gids = (void *) cred->uids + nuids * sizeof (uid_t);
  cred->nuids = nuids;
  cred->ngids = ngids;
  cred->refcnt = 1;

  bcopy (uids, cred->uids, nuids + sizeof (uid_t));
  bcopy (gids, cred->gids, ngids + sizeof (uid_t));

  return cred;
}

/* Return nonzero iff CRED contains user id UID. */
int
cred_has_uid (struct netcred *cred, uid_t uid)
{
  int i;
  for (i = 0; i < cred->nuids; i++)
    if (cred->uids[i] == uid)
      return 1;
  return 0;
}

/* Return nonzero iff CRED contains group id GID. */
int
cred_has_gid (struct netcred *cred, gid_t gid)
{
  int i;
  for (i = 0; i < cred->ngids; i++)
    if (cred->gids[i] == gid)
      return 1;
  return 0;
}
