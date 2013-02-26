/* 
   Copyright (C) 1995, 1996, 2004 Free Software Foundation, Inc.
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
#include <hurd/ports.h>

error_t
netfs_S_io_select (struct protid *user,
		   mach_port_t reply,
		   mach_msg_type_name_t replytype,
		   int *type)
{
  if (!user)
    return EOPNOTSUPP;
  
  *type &= ~SELECT_URG;
  return 0;
}

error_t
netfs_S_io_select_timeout (struct protid *user,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   struct timespec ts,
			   int *type)
{
  return netfs_S_io_select (user, reply, replytype, type);
}
