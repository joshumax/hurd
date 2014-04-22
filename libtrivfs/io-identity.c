/* Fetching identity port
   Copyright (C) 1996,2002 Free Software Foundation, Inc.
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

#include "priv.h"
#include "trivfs_io_S.h"

error_t
trivfs_S_io_identity (struct trivfs_protid *cred,
		      mach_port_t reply,
		      mach_msg_type_name_t replytype,
		      mach_port_t *idport,
		      mach_msg_type_name_t *idport_type,
		      mach_port_t *fsidport,
		      mach_msg_type_name_t *fsidport_type,
		      ino_t *fileno)
{
  error_t err;
  struct stat st;

  if (!cred)
    return EOPNOTSUPP;

  err = io_stat (cred->realnode, &st);
  if (err)
    return err;
  trivfs_modify_stat (cred, &st);

  *idport = cred->po->cntl->file_id;
  *idport_type = MACH_MSG_TYPE_MAKE_SEND;
  *fsidport = cred->po->cntl->filesys_id;
  *fsidport_type = MACH_MSG_TYPE_MAKE_SEND;
  *fileno = st.st_ino;
  return 0;
}
