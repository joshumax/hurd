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
#include <assert.h>
#include <string.h>

kern_return_t
trivfs_S_io_reauthenticate (struct trivfs_protid *cred,
			    mach_port_t reply,
			    mach_msg_type_name_t replytype,
			    mach_port_t rendport)
{
  struct trivfs_protid *newcred;
  error_t err;
  auth_t auth;
  mach_port_t newright;

  if (cred == 0)
    return EOPNOTSUPP;

  do
    err = ports_create_port_noinstall (cred->po->cntl->protid_class,
				       cred->po->cntl->protid_bucket,
				       sizeof (struct trivfs_protid), 
				       &newcred);
  while (err == EINTR);
  if (err)
    return err;

  auth = getauth ();
  newright = ports_get_right (newcred);
  err = mach_port_insert_right (mach_task_self (), newright, newright,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror (err);

  newcred->user = iohelp_reauth (auth, rendport, newright, 1);
  if (idvec_contains (newcred->user->uids, 0))
    newcred->isroot = 1;

  mach_port_deallocate (mach_task_self (), rendport);
  mach_port_deallocate (mach_task_self (), newright);
  mach_port_deallocate (mach_task_self (), auth);

  newcred->hook = cred->hook;
  
  mutex_lock (&cred->po->cntl->lock);
  newcred->po = cred->po;
  newcred->po->refcnt++;
  mutex_unlock (&cred->po->cntl->lock);
  
  do
    err = io_restrict_auth (newcred->po->cntl->underlying, &newcred->realnode,
			    gen_uids, genuidlen, gen_gids, gengidlen);
  while (err == EINTR);
  if (!err && trivfs_protid_create_hook)
    {
      do
	err = (*trivfs_protid_create_hook) (newcred);
      while (err == EINTR);
      if (err)
	mach_port_deallocate (mach_task_self (), newcred->realnode);
    }

  if (err)
    /* Signal that the user destroy hook shouldn't be called on NEWCRED.  */
    newcred->realnode = MACH_PORT_NULL;

  mach_port_move_member (mach_task_self (), newcred->pi.port_right,
			 cred->po->cntl->protid_bucket->portset);

  ports_port_deref (newcred);

  return err;
}
