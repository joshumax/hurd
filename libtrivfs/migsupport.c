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

struct trivfs_protid *
_trivfs_begin_using_protid (mach_port_t port)
{
  if (trivfs_protid_nporttypes > 1)
    {
      struct port_info *pi = ports_get_port (port);
      int i;
      for (i = 0; i < trivfs_protid_nporttypes; i++)
	if (pi->type == trivfs_protid_porttypes[i])
	  return (struct trivfs_protid *) pi;
      ports_done_with_port ((void *)port);
      return 0;
    }
  else
    return ports_check_port_type (port, trivfs_protid_porttypes[0]);
}

void 
_trivfs_end_using_protid (struct trivfs_protid *cred)
{
  ports_done_with_port (cred);
}

struct trivfs_control *
_trivfs_begin_using_control (mach_port_t port)
{
  if (trivfs_cntl_nporttypes > 1)
    {
      struct port_info *pi = ports_get_port (port);
      int i;
      for (i = 0; i < trivfs_cntl_nporttypes; i++)
	if (pi->type == trivfs_cntl_porttypes[i])
	  return (struct trivfs_control *) pi;
      ports_done_with_port ((void *)port);
      return 0;
    }
  else
    return ports_check_port_type (port, trivfs_cntl_porttypes[0]);
}

void 
_trivfs_end_using_control (struct trivfs_control *cred)
{
  ports_done_with_port (cred);
}
