/*
   Copyright (C) 1996,99,2001,02 Free Software Foundation, Inc.

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

#include "iohelp.h"
#include <hurd/auth.h>
#include <sys/mman.h>
#include <stdlib.h>

/* Conduct a reauthentication transaction, returning a new iouser.
   AUTHSERVER is the I/O servers auth port.  The rendezvous port
   provided by the user is REND_PORT.  If the transaction cannot be
   completed, return zero, unless PERMIT_FAILURE is non-zero.  If
   PERMIT_FAILURE is nonzero, then should the transaction fail, return
   an iouser that has no ids.  The new port to be sent to the user is
   newright.  */
error_t iohelp_reauth (struct iouser **user,
		       auth_t authserver, mach_port_t rend_port,
		       mach_port_t newright, int permit_failure)
{
  uid_t gubuf[20], ggbuf[20], aubuf[20], agbuf[20];
  uid_t *gen_uids, *gen_gids, *aux_uids, *aux_gids;
  size_t genuidlen, gengidlen, auxuidlen, auxgidlen;
  error_t err;
  struct iouser *new;

  *user = new = malloc (sizeof (struct iouser));
  if (!new)
    return ENOMEM;

  new->uids = make_idvec ();
  new->gids = make_idvec ();
  if (!new->uids || !new->gids)
    {
      if (new->uids)
	idvec_free (new->uids);
      if (new->gids)
	idvec_free (new->gids);
      free (new);
      return ENOMEM;
    }

  genuidlen = gengidlen = auxuidlen = auxgidlen = 20;
  gen_uids = gubuf;
  gen_gids = ggbuf;
  aux_uids = aubuf;
  aux_gids = agbuf;

  do
    err = auth_server_authenticate (authserver,
				    rend_port,
				    MACH_MSG_TYPE_COPY_SEND,
				    newright,
				    MACH_MSG_TYPE_COPY_SEND,
				    &gen_uids, &genuidlen,
				    &aux_uids, &auxuidlen,
				    &gen_gids, &gengidlen,
				    &aux_gids, &auxgidlen);
  while (err == EINTR);

  if (err)
    {
      if (permit_failure)
	genuidlen = gengidlen = 0;
      else
	goto out;
    }

  err = idvec_set_ids (new->uids, gen_uids, genuidlen);
  if (!err)
    err = idvec_set_ids (new->gids, gen_gids, gengidlen);

  if (gubuf != gen_uids)
    munmap ((caddr_t) gen_uids, genuidlen * sizeof (uid_t));
  if (ggbuf != gen_gids)
    munmap ((caddr_t) gen_gids, gengidlen * sizeof (uid_t));
  if (aubuf != aux_uids)
    munmap ((caddr_t) aux_uids, auxuidlen * sizeof (uid_t));
  if (agbuf != aux_gids)
    munmap ((caddr_t) aux_gids, auxgidlen * sizeof (uid_t));

  if (err)
    {
    out:
      idvec_free (new->uids);
      idvec_free (new->gids);
      free (new);
      *user = 0;
      return err;
    }

  *user = new;
  return 0;
}
