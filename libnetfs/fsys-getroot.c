/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
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
#include "fsys_S.h"

error_t
netfs_S_fsys_getroot (mach_port_t cntl,
		      mach_port_t dotdot,
		      uid_t *uids, mach_msg_type_number_t nuids,
		      uid_t *gids, mach_msg_type_number_t ngids,
		      int flags,
		      retry_type *do_retry,
		      char *retry_name,
		      mach_port_t *retry_port,
		      mach_port_t *retry_port_type)
{
  struct port_info *pt = ports_lookup_port (netfs_port_bucket, cntl,
					    netfs_control_class);
  struct netcred *cred;
  
  if (!pt)
    return EOPNOTSUPP;

  cred = netfs_make_credential (uids, nuids, gids, ngids);
  
  flags &= O_HURD;
  
  mutex_lock (&netfs_root_node->lock);
  netfs_validate_stat (netfs_root_node);
  type = 
