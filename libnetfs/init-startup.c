/* 
   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <error.h>
#include <hurd/fsys.h>
#include "netfs.h"

mach_port_t
netfs_startup (mach_port_t bootstrap, int flags)
{
  mach_port_t realnode;
  struct port_info *newpi;
  
  if (bootstrap == MACH_PORT_NULL)
    error (10, 0, "Must be started as a translator");

  errno = ports_create_port (netfs_control_class, netfs_port_bucket,
			     sizeof (struct port_info), &newpi);
  if (!errno)
    {
      errno = fsys_startup (bootstrap, flags, ports_get_right (newpi),
			    MACH_MSG_TYPE_MAKE_SEND, &realnode);
      ports_port_deref (newpi);
    }
  if (errno)
    error (11, errno, "Translator startup failure: fsys_startup");

  mach_port_deallocate (mach_task_self (), bootstrap);

  return realnode;
}
