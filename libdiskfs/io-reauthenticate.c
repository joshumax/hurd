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
#include "io_S.h"

/* Implement io_reathenticate as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_reauthenticate (struct protid *cred,
			    mach_port_t rend_port)
{
  struct protid *newcred;
  uid_t gubuf[20], ggbuf[20], aubuf[20], agbuf[20];
  uid_t *gen_uids, *gen_gids, *aux_uids, *aux_gids;
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;

  if (cred == 0)
    return EOPNOTSUPP;

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;

  mutex_lock (&cred->po->np->lock);
  err = diskfs_start_protid (cred->po, &newcred);
  if (err)
    {
      mutex_unlock (&cred->po->np->lock);
      return err;
    }

  do
    err = auth_server_authenticate (diskfs_auth_server_port,
				    rend_port,
				    MACH_MSG_TYPE_MOVE_SEND,
				    ports_get_right (newcred),
				    MACH_MSG_TYPE_MAKE_SEND,
				    &gen_uids, &genuidlen,
				    &aux_uids, &auxuidlen,
				    &gen_gids, &gengidlen,
				    &aux_gids, &auxgidlen);
  while (err == EINTR);
    
  if (err)
    diskfs_finish_protid (newcred, 0, 0, 0, 0);
  else
    diskfs_finish_protid (newcred, gen_uids, genuidlen, gen_gids, gengidlen);
  mutex_unlock (&cred->po->np->lock);

  ports_port_deref (newcred);

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

  return 0;
}
