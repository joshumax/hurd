/* Unimplemented rpcs from <hurd/fs.defs>

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

error_t
netfs_S_file_notice_changes (struct protid *user,
			     mach_port_t port)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_file_get_translator (struct protid *user,
			     char **translator,
			     mach_msg_type_number_t *translen)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_file_get_translator_cntl (struct protid *user,
				  mach_port_t *trans,
				  mach_msg_type_name_t *transtype)
{
  return EOPNOTSUPP;
}

error_t 
netfs_S_file_set_translator (struct protid *user,
			     int pflags, int aflags,
			     int gflags, char *passive,
			     mach_msg_type_number_t passivelen,
			     mach_port_t active)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_file_exec (struct protid *user,
		   task_t task,
		   int flags,
		   char *argv, mach_msg_type_number_t argvlen,
		   char *envp, mach_msg_type_number_t envplen,
		   mach_port_t *fds, mach_msg_type_number_t nfds,
		   mach_port_t *ports, mach_msg_type_number_t nports,
		   int *ints, mach_msg_type_number_t nints,
		   mach_port_t *dealports, mach_msg_type_number_t ndealports,
		   mach_port_t *destports, mach_msg_type_number_t ndestports)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_file_getfh (struct protid *user,
		    char **data, mach_msg_type_number_t *ndata)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_ifsock_getsockaddr (struct protid *user,
			    mach_port_t *address)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_io_pathconf (struct protid *user,
		     int name,
		     int *value)
{
  return EOPNOTSUPP;
}

error_t
netfs_S_io_server_version (struct protid *user,
			   char *name,
			   int *major,
			   int *minor,
			   int *edit)
{
  return EOPNOTSUPP;
}
