/*
   Copyright (C) 1994,95,96,2000,01 Free Software Foundation, Inc.

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
  error_t err;
  mach_port_t newright;
  struct iouser *user;

  if (cred == 0)
    return EOPNOTSUPP;

  /* This routine must carefully ignore EINTR because we
     are a simpleroutine, so callers won't know to restart. */

  pthread_mutex_lock (&cred->po->np->lock);
  refcount_ref (&cred->po->refcnt);
  do
    err = diskfs_start_protid (cred->po, &newcred);
  while (err == EINTR);
  if (err)
    {
      refcount_deref (&cred->po->refcnt);
      pthread_mutex_unlock (&cred->po->np->lock);
      return err;
    }

  newright = ports_get_send_right (newcred);
  assert_backtrace (newright != MACH_PORT_NULL);

  /* Release the node lock while blocking on the auth server and client.  */
  pthread_mutex_unlock (&cred->po->np->lock);
  err = iohelp_reauth (&user, diskfs_auth_server_port, rend_port,
		       newright, 1);
  pthread_mutex_lock (&cred->po->np->lock);
  if (! err)
    {
      diskfs_finish_protid (newcred, user);
      iohelp_free_iouser (user);
      mach_port_deallocate (mach_task_self (), rend_port);
    }

  mach_port_deallocate (mach_task_self (), newright);

  pthread_mutex_unlock (&cred->po->np->lock);

  ports_port_deref (newcred);

  return err;
}
