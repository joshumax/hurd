/*
   Copyright (C) 1994, 2002, 2010 Free Software Foundation, Inc.

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
#include "trivfs_fs_S.h"

kern_return_t
trivfs_S_file_exec (trivfs_protid_t exec_file,
		    mach_port_t reply,
		    mach_msg_type_name_t replyPoly,
		    mach_port_t exec_task,
		    int flags,
		    data_t argv,
		    mach_msg_type_number_t argvCnt,
		    data_t envp,
		    mach_msg_type_number_t envpCnt,
		    portarray_t fdarray,
		    mach_msg_type_number_t fdarrayCnt,
		    portarray_t portarray,
		    mach_msg_type_number_t portarrayCnt,
		    intarray_t intarray,
		    mach_msg_type_number_t intarrayCnt,
		    mach_port_array_t deallocnames,
		    mach_msg_type_number_t deallocnamesCnt,
		    mach_port_array_t destroynames,
		    mach_msg_type_number_t destroynamesCnt)
{
  return EOPNOTSUPP;
}

kern_return_t
trivfs_S_file_exec_paths (trivfs_protid_t exec_file,
			  mach_port_t reply,
			  mach_msg_type_name_t replyPoly,
			  mach_port_t exec_task,
			  int flags,
			  string_t path,
			  string_t abspath,
			  data_t argv,
			  mach_msg_type_number_t argvCnt,
			  data_t envp,
			  mach_msg_type_number_t envpCnt,
			  portarray_t fdarray,
			  mach_msg_type_number_t fdarrayCnt,
			  portarray_t portarray,
			  mach_msg_type_number_t portarrayCnt,
			  intarray_t intarray,
			  mach_msg_type_number_t intarrayCnt,
			  mach_port_array_t deallocnames,
			  mach_msg_type_number_t deallocnamesCnt,
			  mach_port_array_t destroynames,
			  mach_msg_type_number_t destroynamesCnt)
{
  return EOPNOTSUPP;
}
