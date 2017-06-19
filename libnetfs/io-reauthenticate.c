/*
   Copyright (C) 1995,96,2000,01 Free Software Foundation, Inc.
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
  error_t err;
  struct protid *newpi;
  mach_port_t newright;

  if (!user)
    return EOPNOTSUPP;

  /* This routine must carefully ignore EINTR because we
     are a simpleroutine, so callers won't know to restart. */

  refcount_ref (&user->po->refcnt);
  pthread_mutex_lock (&user->po->np->lock);
  do
    newpi = netfs_make_protid (user->po, 0);
  while (! newpi && errno == EINTR);
  if (! newpi)
    {
      refcount_deref (&user->po->refcnt);
      pthread_mutex_unlock (&user->po->np->lock);
      return errno;
    }

  newright = ports_get_send_right (newpi);
  assert_backtrace (newright != MACH_PORT_NULL);

  /* Release the node lock while blocking on the auth server and client.  */
  pthread_mutex_unlock (&user->po->np->lock);
  err = iohelp_reauth (&newpi->user, netfs_auth_server_port, rend_port,
		       newright, 1);
  pthread_mutex_lock (&user->po->np->lock);

  if (!err)
    mach_port_deallocate (mach_task_self (), rend_port);
  mach_port_deallocate (mach_task_self (), newright);

  mach_port_move_member (mach_task_self (), newpi->pi.port_right,
			 netfs_port_bucket->portset);

  pthread_mutex_unlock (&user->po->np->lock);
  ports_port_deref (newpi);

  return err;
}
