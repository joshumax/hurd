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
  uid_t gubuf[20], ggbuf[20], aubuf[20], agbuf[20];
  uid_t *gen_uids, *gen_gids, *aux_uids, *aux_gids;
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;
  int i;
  auth_t auth;
  mach_port_t newright;

  if (cred == 0)
    return EOPNOTSUPP;

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;

  mutex_lock (&global_lock);

  do
    err = ports_create_port (cred->po->cntl->protid_class,
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
  do
    err = auth_server_authenticate (auth, 
				    rendport,
				    MACH_MSG_TYPE_COPY_SEND,
				    newright,
				    MACH_MSG_TYPE_COPY_SEND,
				    &gen_uids, &genuidlen, 
				    &aux_uids, &auxuidlen,
				    &gen_gids, &gengidlen,
				    &aux_gids, &auxgidlen);
  while (err == EINTR);
  mach_port_deallocate (mach_task_self (), rendport);
  mach_port_deallocate (mach_task_self (), newright);
  mach_port_deallocate (mach_task_self (), auth);

  if (err)
    {
      newcred->isroot = 0;
      newcred->uids = malloc (1);
      newcred->gids = malloc (1);
      newcred->nuids = 0;
      newcred->ngids = 0;
    }
  else
    {
      newcred->isroot = 0;
      for (i = 0; i < genuidlen; i++)
	if (gen_uids[i] == 0)
	  newcred->isroot = 1;
      
      newcred->uids = malloc (genuidlen * sizeof (uid_t));
      newcred->gids = malloc (gengidlen * sizeof (uid_t));
      bcopy (gen_uids, newcred->uids, genuidlen * sizeof (uid_t));
      bcopy (gen_gids, newcred->gids, gengidlen * sizeof (uid_t));
      newcred->nuids = genuidlen;
      newcred->ngids = gengidlen;
    }
  
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

  mutex_unlock (&global_lock);

  if (gubuf != gen_uids)
    vm_deallocate (mach_task_self (), (u_int) gen_uids,
		   genuidlen * sizeof (uid_t));
  if (ggbuf != gen_gids)
    vm_deallocate (mach_task_self (), (u_int) gen_gids,
		   gengidlen * sizeof (uid_t));
  if (aubuf != aux_uids)
    vm_deallocate (mach_task_self (), (u_int) aux_uids,
		   auxuidlen * sizeof (uid_t));
  if (agbuf != aux_gids)
    vm_deallocate (mach_task_self (), (u_int) aux_gids,
		   auxgidlen * sizeof (uid_t));

  ports_port_deref (newcred);

  return err;
}
