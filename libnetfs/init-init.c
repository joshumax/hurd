/* 
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
#include <error.h>

/* For safe inlining of netfs_node_netnode and netfs_netnode_node.  */
size_t const _netfs_sizeof_struct_node = sizeof (struct node);

struct node *netfs_root_node = 0;
struct port_bucket *netfs_port_bucket = 0;
struct port_class *netfs_protid_class = 0;
struct port_class *netfs_control_class = 0;
auth_t netfs_auth_server_port = 0;
mach_port_t netfs_fsys_identity;
volatile struct mapped_time_value *netfs_mtime;


void
netfs_init ()
{
  error_t err;
  err = maptime_map (0, 0, &netfs_mtime);
  if (err)
    error (2, err, "mapping time");

  netfs_protid_class = ports_create_class (netfs_release_protid, 0);
  netfs_control_class = ports_create_class (0, 0);
  netfs_port_bucket = ports_create_bucket ();
  netfs_auth_server_port = getauth ();
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, 
		      &netfs_fsys_identity);
}
