/* diskfs_startup_diskfs -- advertise our fsys control port to our parent FS.
   Copyright (C) 1994, 1995 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Roland McGrath.  */

#include "priv.h"
#include <stdio.h>
#include <hurd/fsys.h>

mach_port_t
diskfs_startup_diskfs (mach_port_t bootstrap)
{
  mach_port_t realnode;
  struct port_info *newpi;
  
  if (bootstrap != MACH_PORT_NULL)
    {
      _diskfs_ncontrol_ports++;
      newpi = ports_allocate_port (diskfs_port_bucket,
				   sizeof (struct port_info),
				   diskfs_control_class);
      errno = fsys_startup (bootstrap, ports_get_right (newpi),
			    MACH_MSG_TYPE_MAKE_SEND, &realnode);
      ports_port_deref (newpi);
      if (errno)
	{
	  perror ("Translator startup failure: fsys_startup");
	  exit (1);
	}
      mach_port_deallocate (mach_task_self (), bootstrap);
    }
  else
    realnode = MACH_PORT_NULL;

  if (diskfs_default_sync_interval)
    /* Start 'em sync'n */
    diskfs_set_sync_interval (diskfs_default_sync_interval);

  return realnode;
}
