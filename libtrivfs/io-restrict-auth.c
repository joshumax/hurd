/*
   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "io_S.h"
#include <string.h>

/* Tell if the array LIST (of size N) contains a member equal to QUERY. */
static inline int
listmember (int *list, int query, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (list[i] == query)
      return 1;
  return 0;
}

kern_return_t
trivfs_S_io_restrict_auth (struct trivfs_protid *cred,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   mach_port_t *newport,
			   mach_msg_type_name_t *newporttype,
			   uid_t *uids, u_int nuids,
			   uid_t *gids, u_int ngids)
{
  int i;
  error_t err = 0;
  struct trivfs_protid *newcred;
  uid_t *newuids, *newgids;
  int newnuids, newngids;
  
  if (!cred)
    return EOPNOTSUPP;
  
  if (cred->isroot)
    /* CRED has root access, and so may use any ids.  */
    {
      newuids = uids;
      newnuids = nuids;
      newgids = gids;
      newngids = ngids;
    }
  else
    /* Otherwise, use any of the requested ids that CRED already has.  */
    {
      newuids = alloca (sizeof (uid_t) * cred->nuids);
      newgids = alloca (sizeof (uid_t) * cred->ngids);
      for (i = newnuids = 0; i < cred->nuids; i++)
	if (listmember (uids, cred->uids[i], nuids))
	  newuids[newnuids++] = cred->uids[i];
      for (i = newngids = 0; i < cred->gids[i]; i++)
	if (listmember (gids, cred->gids[i], ngids))
	  newgids[newngids++] = cred->gids[i];
    }

  err = ports_create_port (cred->po->cntl->protid_class,
			   cred->po->cntl->protid_bucket,
			   sizeof (struct trivfs_protid), 
			   &newcred);
  if (err)
    return err;

  newcred->isroot = 0;
  mutex_lock (&cred->po->cntl->lock);
  newcred->po = cred->po;
  newcred->po->refcnt++;
  mutex_unlock (&cred->po->cntl->lock);
  if (cred->isroot)
    {
      for (i = 0; i < nuids; i++)
	if (uids[i] == 0)
	  newcred->isroot = 1;
    }
  newcred->gids = malloc (newngids * sizeof (uid_t));
  newcred->uids = malloc (newnuids * sizeof (uid_t));
  bcopy (newuids, newcred->uids, newnuids * sizeof (uid_t));
  bcopy (newgids, newcred->gids, newngids * sizeof (uid_t));
  newcred->ngids = newngids;
  newcred->nuids = newnuids;
  newcred->hook = cred->hook;

  err = io_restrict_auth (cred->realnode, &newcred->realnode, 
			  newuids, newnuids, newgids, newngids);
  if (!err && trivfs_protid_create_hook)
    {
      err = (*trivfs_protid_create_hook) (newcred);
      if (err)
	mach_port_deallocate (mach_task_self (), newcred->realnode);
    }

  if (err)
    /* Signal that the user destroy hook shouldn't be called on NEWCRED.  */
    newcred->realnode = MACH_PORT_NULL;
  else
    {
      *newport = ports_get_right (newcred);
      *newporttype = MACH_MSG_TYPE_MAKE_SEND;
    }

  /* This will destroy NEWCRED if we got an error and didn't do the
     ports_get_right above.  */
  ports_port_deref (newcred);

  return 0;
}
