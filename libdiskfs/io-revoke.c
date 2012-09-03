/*
   Copyright (C) 1999 Free Software Foundation
   Written by Thomas Bushnell, BSG.

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

/* Implement io_revoke as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_revoke (struct protid *cred)
{
  error_t err;
  struct node *np;

  error_t
    iterator_function (void *port)
    {
      struct protid *user = port;

      if ((user != cred)
	  && (user->po->np == np))
	ports_destroy_right (user);
      return 0;
    }

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  err = fshelp_isowner (&np->dn_stat, cred->user);

  pthread_mutex_unlock (&np->lock);

  if (err)
    return err;

  ports_inhibit_bucket_rpcs (diskfs_port_bucket);
  ports_class_iterate (diskfs_protid_class, iterator_function);
  ports_resume_bucket_rpcs (diskfs_port_bucket);

  return 0;
}
