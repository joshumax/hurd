/* 
   Copyright (C) 1994 Free Software Foundation

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
error_t
S_io_reauthenticate (struct protid *cred, 
		     int rend_int)
{
  struct protid *newcred;
  uid_t *gen_uids = alloca (sizeof (uid_t) * 20);
  uid_t *gen_gids = alloca (sizeof (uid_t) * 20);
  uid_t *aux_uids = alloca (sizeof (uid_t) * 20);
  uid_t *aux_gids = alloca (sizeof (uid_t) * 20);
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  uid_t *gubuf, *ggbuf, *aubuf, *agbuf;
  error_t err;

  if (cred == 0)
    return EOPNOTSUPP;

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gubuf = gen_uids; ggbuf = gen_gids;
  aubuf = aux_uids; agbuf = aux_gids;

  mutex_lock (&cred->po->np->lock);
  newcred = diskfs_start_protid (cred->po);
  err = auth_server_authenticate (auth_server_port, 
				  cred->fspt.pi.port,
				  MACH_MSG_TYPE_MAKE_SEND,
				  rend_int,
				  newcred->fspt.pi.port,
				  MACH_MSG_TYPE_MAKE_SEND,
				  &gen_uids, &genuidlen, 
				  &aux_uids, &auxuidlen,
				  &gen_gids, &gengidlen,
				  &aux_gids, &auxgidlen);
  assert (!err);		/* XXX */

  diskfs_finish_protid (newcred, gen_uids, genuidlen, gen_gids, gengidlen);

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
