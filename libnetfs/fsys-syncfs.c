/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
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
#include "fsys_S.h"

error_t
netfs_S_fsys_syncfs (mach_port_t cntl,
		     int wait,
		     int children)
{
  struct iouser *cred;
  uid_t root = 0;
  error_t err;

  cred = iohelp_create_iouser (make_idvec (), make_idvec ());
  idvec_set_ids (cred->uids, &root, 1);
  idvec_set_ids (cred->gids, &root, 1);
  err = netfs_attempt_syncfs (cred, wait);
  iohelp_free_iouser (cred);
  return err;
}
