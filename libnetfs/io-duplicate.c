/* 
   Copyright (C) 1995,96,2001 Free Software Foundation, Inc.
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
netfs_S_io_duplicate (struct protid *user,
		      mach_port_t *newport,
		      mach_msg_type_name_t *newporttp)
{
  error_t err;
  struct protid *newpi;
  struct iouser *clone;

  err = iohelp_dup_iouser (&clone, user->user);
  if (err)
    return err;
  
  refcount_ref (&user->po->refcnt);
  pthread_mutex_lock (&user->po->np->lock);
  newpi = netfs_make_protid (user->po, clone);
  *newport = ports_get_right (newpi);
  pthread_mutex_unlock (&user->po->np->lock);
  *newporttp = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newpi);
  return 0;
}
