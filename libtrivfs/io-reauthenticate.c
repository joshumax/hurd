/*
   Copyright (C) 1993, 1994 Free Software Foundation

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

error_t
trivfs_S_io_reauthenticate (struct protid *cred,
			    int rendint)
{
  struct protid *newcred;
  uid_t *gen_uids = alloca (sizeof (uid_t) * 20);
  uid_t *gen_gids = alloca (sizeof (uid_t) * 20);
  uid_t *aux_uids = alloca (sizeof (uid_t) * 20);
  uid_t *aux_gids = alloca (sizeof (uid_t) * 20);
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  uid_t *gubuf, *ggbuf, *aubuf, *agbuf;
  error_t err;
  int i;

  if (cred == 0)
    return EOPNOTSUPP;

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gubuf = gen_uids; ggbuf = gen_gids;
  aubuf = aux_uids; agbuf = aux_gids;

  newcred = ports_allocate_port (sizeof (struct protid), 
				 trivfs_protid_porttype);
  err = auth_server_authenticate (diskfs_auth_server_port, 
				  ports_get_right (cred),
				  MACH_MSG_TYPE_MAKE_SEND,
				  rend_int,
				  ports_get_right (newcred),
				  MACH_MSG_TYPE_MAKE_SEND,
				  &gen_uids, &genuidlen, 
				  &aux_uids, &auxuidlen,
				  &gen_gids, &gengidlen,
				  &aux_gids, &auxgidlen);
  assert (!err);		/* XXX */

  newcred->isroot = 0;
  for (i = 0; i < genuidlen; i++)
    if (gen_uids[i] == 0)
      newcred->isroot = 1;
  newcred->cntl = cred->cntl;
  ports_port_ref (newcred->cntl);
  err = io_restrict_auth (newcred->cntl->underlying, &newcred->realnode,
			  gen_uids, genuidlen, gen_gids, gengidlen);
  if (err)
    newcred->realnode = MACH_PORT_NULL;

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
