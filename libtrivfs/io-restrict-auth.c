/*
   Copyright (C) 1993,94,95,96,2001,02 Free Software Foundation, Inc.

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
#include "trivfs_io_S.h"
#include <string.h>

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

kern_return_t
trivfs_S_io_restrict_auth (struct trivfs_protid *cred,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   mach_port_t *newport,
			   mach_msg_type_name_t *newporttype,
			   uid_t *uids, size_t nuids,
			   uid_t *gids, size_t ngids)
{
  unsigned int i;
  error_t err;
  struct trivfs_protid *newcred;
  struct idvec *uvec, *gvec;
  struct iouser *user;

  if (!cred)
    return EOPNOTSUPP;

  if (cred->isroot)
    /* CRED has root access, and so may use any ids.  */
    {
      err = iohelp_create_complex_iouser (&user, uids, nuids, gids, ngids);
      if (err)
        return err;
    }
  else
    {
      uvec = make_idvec ();
      if (! uvec)
        return ENOMEM;

      gvec = make_idvec ();
      if (! gvec)
        {
	  idvec_free (uvec);
	  return ENOMEM;
	}

      /* Otherwise, use any of the requested ids that CRED already has.  */
      for (i = 0; i < cred->user->uids->num; i++)
	if (listmember (uids, cred->user->uids->ids[i], nuids))
	  {
	    err = idvec_add (uvec, cred->user->uids->ids[i]);
	    if (err)
	      goto out;
	  }

      for (i = 0; i < cred->user->gids->num; i++)
	if (listmember (gids, cred->user->gids->ids[i], ngids))
	  {
	    err = idvec_add (gvec, cred->user->gids->ids[i]);
	    if (err)
	      goto out;
	  }

      err = iohelp_create_iouser (&user, uvec, gvec);
      if (err)
        {
	out:
	  idvec_free (uvec);
	  idvec_free (gvec);
	  return err;
	}
    }

  err = ports_create_port (cred->po->cntl->protid_class,
			   cred->po->cntl->protid_bucket,
			   sizeof (struct trivfs_protid),
			   &newcred);
  if (err)
    {
      iohelp_free_iouser (user);
      return err;
    }

  newcred->po = cred->po;
  refcount_ref (&newcred->po->refcnt);
  newcred->isroot = cred->isroot && _is_privileged (user->uids);
  newcred->user = user;
  newcred->hook = cred->hook;

  err = io_restrict_auth (cred->realnode, &newcred->realnode,
			  user->uids->ids, user->uids->num,
			  user->gids->ids, user->gids->num);
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
