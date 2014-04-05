/* Dead name notification

   Copyright (C) 1995, 1999 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "ports.h"
#include "notify_S.h"

error_t
ports_do_mach_notify_dead_name (struct port_info *pi,
				mach_port_t dead_name)
{
  if (!pi)
    return EOPNOTSUPP;
  ports_dead_name (pi, dead_name);

  /* Drop gratuitous extra reference that the notification creates. */
  mach_port_deallocate (mach_task_self (), dead_name);
  
  return 0;
}
