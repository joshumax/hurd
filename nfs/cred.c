/* NFS credential manipulation
   Copyright (C) 1995 Free Software Foundation, Inc.
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

static spin_lock_t cred_refcnt_lock = SPIN_LOCK_INITIALIZER;

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

struct netcred *
netfs_copy_credential (struct netcred *cred)
{
  spin_lock (&cred_refcnt_lock);
  cred->refcnt++;
  spin_unlock (&cred_refcnt_lock);
  return cred;
}

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
