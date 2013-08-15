/* 
   Copyright (C) 1996,97,2000,02 Free Software Foundation, Inc.
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
  error_t err;
  mach_port_t realnode, right;
  struct port_info *newpi;
  
  if (bootstrap == MACH_PORT_NULL)
    error (10, 0, "Must be started as a translator");

  err = ports_create_port (netfs_control_class, netfs_port_bucket,
			     sizeof (struct port_info), &newpi);
  if (!err)
    {
      right = ports_get_send_right (newpi);
      err = fsys_startup (bootstrap, flags, right, MACH_MSG_TYPE_COPY_SEND,
			    &realnode);
      mach_port_deallocate (mach_task_self (), right);
      ports_port_deref (newpi);
    }
  if (err)
    error (11, err, "Translator startup failure: fsys_startup");

  mach_port_deallocate (mach_task_self (), bootstrap);

  /* Mark us as important.  */
  mach_port_t proc = getproc ();
  if (proc == MACH_PORT_NULL)
    error (12, err, "Translator startup failure: getproc");

  err = proc_mark_important (proc);

  /* This might fail due to permissions or because the old proc server
     is still running, ignore any such errors.  */
  if (err && err != EPERM && err != EMIG_BAD_ID)
    error (13, err, "Translator startup failure: proc_mark_important");

  mach_port_deallocate (mach_task_self (), proc);

  return realnode;
}
