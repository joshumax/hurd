/*
   Copyright (C) 1993,94,96,2002 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "trivfs_io_S.h"
#include <unistd.h>

kern_return_t
trivfs_S_io_stat (struct trivfs_protid *cred,
		  mach_port_t reply,
		  mach_msg_type_name_t replytype,
		  struct stat *st)
{
  error_t err;
  if (!cred)
    return EOPNOTSUPP;

  err = io_stat (cred->realnode, st);

  if (!err)
    {
      if (! trivfs_fsid)
	trivfs_fsid = getpid();

      st->st_fstype = trivfs_fstype;
      st->st_fsid = trivfs_fsid;
      st->st_mode = (st->st_mode & ~S_IFMT & ~S_ITRANS) | S_IFCHR | S_IROOT;

      trivfs_modify_stat (cred, st);
    }

  return err;
}
