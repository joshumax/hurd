/* Stub for the file_get_storage_info RPC as described in <hurd/fs.defs>.
   Copyright (C) 1995 Free Software Foundation, Inc.

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
#include "fs_S.h"

error_t
trivfs_S_file_get_storage_info (struct trivfs_protid *cred, int *class,
				int **runs, mach_msg_type_number_t *runs_len,
				char *dev_name, mach_port_t *dev_port,
				mach_msg_type_name_t *dev_port_type,
				char **misc, mach_msg_type_number_t *misc_len)
{
  return EOPNOTSUPP;
}
