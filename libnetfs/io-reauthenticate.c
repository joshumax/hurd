/* 
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

#include "netfs.h"
#include "io_S.h"

error_t
netfs_S_io_reauthenticate (struct protid *user, mach_port_t rend_port)
{
  struct protid *newpi;
  uid_t gubuf[20], ggbuf[20], aubuf[20], agbuf[20];
  uid_t *gen_uids, *gen_gids, *aux_uids, *aux_gids;
  u_int genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;
  
  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;
  
  mutex_lock (&user->po->np->lock);
  newpi = netfs_make_protid (user->po, 0);
  err = auth_server_authenticate (netfs_auth_server_port,
				  rend_port,
				  MACH_MSG_TYPE_COPY_SEND,
				  ports_get_right (newpi),
				  MACH_MSG_TYPE_MAKE_SEND,
				  &gen_uids, &genuidlen,
				  &aux_uids, &auxuidlen,
				  &gen_gids, &gengidlen,
				  &aux_uids, &auxuidlen);
  mach_port_deallocate (mach_task_self (), rend_port);
  assert_perror (err);
  
  newpi->credential = netfs_make_credential (gen_uids, genuidlen,
					     gen_gids, gengidlen);
  mutex_unlock (&user->po->np->lock);
  ports_port_deref (newpi);

  if (gen_uids != gubuf)
    vm_deallocate (mach_task_self (), (vm_address_t) gen_uids,
		   genuidlen * sizeof (uid_t));
  if (aux_uids != aubuf)
    vm_deallocate (mach_task_self (), (vm_address_t) aux_uids,
		   auxuidlen * sizeof (uid_t));
  if (gen_gids != ggbuf)
    vm_deallocate (mach_task_self (), (vm_address_t) gen_gids,
		   gengidlen * sizeof (uid_t));
  if (aux_gids != agbuf)
    vm_deallocate (mach_task_self (), (vm_address_t) aux_gids,
		   auxgidlen * sizeof (uid_t));
  return 0;
}
