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

#include <priv.h>

mach_port_t
trivfs_handle_port (mach_port_t realnode, 
		    int cntltype,
		    int protidtype)
{
  struct trivfs_control *cntl;
  
  cntl = ports_allocate_port (sizeof (struct trivfs_control), cntltype);
  cntl->underlying = realnode;
  cntl->protidtypes = protidtype;
  return ports_get_right (cntl);
}
