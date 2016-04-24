/*
   Copyright (C) 1995,96,2001,02 Free Software Foundation, Inc.
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
netfs_S_io_restrict_auth (struct protid *user,
			  mach_port_t *newport,
			  mach_msg_type_name_t *newporttype,
			  uid_t *uids,
			  mach_msg_type_number_t nuids,
			  gid_t *gids,
			  mach_msg_type_number_t ngids)
{
  error_t err;
  struct protid *newpi;
  struct iouser *new_user;

  if (!user)
    return EOPNOTSUPP;

  err = iohelp_restrict_iouser (&new_user, user->user,
				uids, nuids, gids, ngids);
  if (err)
    return err;

  refcount_ref (&user->po->refcnt);
  newpi = netfs_make_protid (user->po, new_user);
  if (newpi)
    {
      *newport = ports_get_right (newpi);
      *newporttype = MACH_MSG_TYPE_MAKE_SEND;
    }
  else
    {
      refcount_deref (&user->po->refcnt);
      iohelp_free_iouser (new_user);
      err = ENOMEM;
    }

  ports_port_deref (newpi);
  return err;
}
