/*
   Copyright (C) 1994, 2002, 2015-2019 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "priv.h"
#include "trivfs_fs_S.h"

#include <fcntl.h>
#include <sys/file.h>

kern_return_t
trivfs_S_file_lock_stat (struct trivfs_protid *cred,
			 mach_port_t reply, mach_msg_type_name_t reply_type,
			 int *mystatus, int *otherstatus)
{
  return EOPNOTSUPP;
}
