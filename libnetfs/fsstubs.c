/* Unimplemented rpcs from <hurd/fs.defs>

   Copyright (C) 1995, 1996, 1999 Free Software Foundation, Inc.
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
#include "fs_S.h"
#include "ifsock_S.h"

error_t __attribute__((weak))
netfs_S_file_notice_changes (struct protid *user,
			     mach_port_t port)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_file_getfh (struct protid *user,
		    data_t *data, mach_msg_type_number_t *ndata)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_ifsock_getsockaddr (struct protid *user,
			    mach_port_t *address)
{
  return EOPNOTSUPP;
}
