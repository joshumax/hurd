/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

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
#include "io_S.h"
#include <assert.h>

kern_return_t
trivfs_S_io_map_segment (struct trivfs_protid *cred,
			 mach_port_t reply,
			 mach_msg_type_name_t replytype,
			 int index,
			 mach_port_t *rdobj,
			 mach_msg_type_name_t *rdtype,
			 mach_port_t *wrobj,
			 mach_msg_type_name_t *wrtype)
{
  assert (!trivfs_support_read && !trivfs_support_write);
  return EOPNOTSUPP;
}
