/* 
   Copyright (C) 1994 Free Software Foundation

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

/* Implement io_map as described in <hurd/io.defs>. */
error_t
S_io_map (struct protid *cred,
	  memory_object_t *rdobj,
	  mach_msg_type_name_t *rdtype,
	  memory_object_t *wrobj,
	  mach_msg_type_name_t *wrtype)
{
  if (!cred)
    return EOPNOTSUPP;
  
  *rdobj = diskfs_get_filemap (cred->po->np);
  *rdtype = *wrtype = MACH_MSG_TYPE_MAKE_SEND;
  *wrobj = *rdobj;
  return 0;
}
