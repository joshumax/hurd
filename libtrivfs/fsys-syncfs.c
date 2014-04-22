/*
   Copyright (C) 1994,96,2002 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"
#include "trivfs_fsys_S.h"

kern_return_t
trivfs_S_fsys_syncfs (struct trivfs_control *cntl,
		      mach_port_t reply,
		      mach_msg_type_name_t replytype,
		      int wait,
		      int dochildren)
{
  return cntl ? file_sync (cntl->underlying, wait, 0) : EOPNOTSUPP;
}
