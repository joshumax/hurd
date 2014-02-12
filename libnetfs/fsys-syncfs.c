/* 
   Copyright (C) 1996, 2001 Free Software Foundation, Inc.
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
netfs_S_fsys_syncfs (struct netfs_control *cntl,
		     mach_port_t reply,
		     mach_msg_type_name_t reply_type,
		     int wait,
		     int children)
{
  struct iouser *cred;
  error_t err;

  err = iohelp_create_simple_iouser (&cred, 0, 0);
  if (err)
    return err;
  err = netfs_attempt_syncfs (cred, wait);
  iohelp_free_iouser (cred);
  return err;
}
