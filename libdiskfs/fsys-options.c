/* Parse run-time options

   Copyright (C) 1995 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <errno.h>
#include <argz.h>
#include <hurd/fsys.h>

#include "priv.h"
#include "fsys_S.h"

/* Implement fsys_set_options as described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_set_options (fsys_t fsys,
			   char *data, mach_msg_type_number_t len,
			   int do_children)
{
  int argc = argz_count (data, len);
  char **argv = alloca (sizeof (char *) * (argc + 1));
  struct port_info *pt = ports_lookup_port (diskfs_port_bucket, fsys,
					    diskfs_control_class);
  int ret;
  error_t
    helper (struct node *np)
      {
	error_t error;
	mach_port_t control;
	
	error = fshelp_fetch_control (np, &control);
	if (!error && (control != MACH_PORT_NULL))
	  {
	    error = fsys_set_options (control, data, len, do_children);
	    mach_port_deallocate (mach_task_self (), control);
	  }
	else
	  error = 0;
	return error;
      }
  
  if (!pt)
    return EOPNOTSUPP;

  if (do_children)
    {
      ret = diskfs_node_iterate (helper);
      if (ret)
	return ret;
    }

  argz_extract (data, len, argv);

  ret = diskfs_set_options (argc, argv);

  ports_port_deref (pt);
  return ret;
}
