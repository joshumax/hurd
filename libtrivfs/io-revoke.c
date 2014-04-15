/*
   Copyright (C) 1999,2002 Free Software Foundation, Inc.
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
#include "trivfs_io_S.h"

/* Implement io_revoke as described in <hurd/io.defs>. */
kern_return_t
trivfs_S_io_revoke (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type)
{
  /* Revoke of the underlying node is actually generally right,
     because that will cause actual calls to fail.  In any case,
     we don't have the ability to check permissions ourselves
     correctly. */

  return cred ? io_revoke (cred->realnode) : EOPNOTSUPP;
}
