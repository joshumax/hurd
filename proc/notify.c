/* Handle notifications
   Copyright (C) 1992, 1993, 1994, 1996, 1999 Free Software Foundation, Inc.

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

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <mach/notify.h>
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>

#include "proc.h"
#include "notify_S.h"

/* We ask for dead name notifications to detect when tasks and
   message ports die.  Both notifications get sent to the process
   port.   */
kern_return_t
do_mach_notify_dead_name (struct port_info *pi,
			  mach_port_t deadport)
{
  struct proc *p;

  if (!pi)
    return EOPNOTSUPP;

  if (pi->port_right == generic_port)
    {
      check_dead_execdata_notify (deadport);
      mach_port_deallocate (mach_task_self (), deadport);
      return 0;
    }

  p = (struct proc *) pi;

  if (p->p_pi.bucket != proc_bucket
      || p->p_pi.class != proc_class)
    return EOPNOTSUPP;

  if (p->p_task == deadport)
    {
      process_has_exited (p);
      mach_port_deallocate (mach_task_self (), deadport);
      return 0;
    }
  else
    {
      return EINVAL;
    }
}

/* We get no-senders notifications on exception ports that we
   handle through proc_handle_exceptions. */
kern_return_t
do_mach_notify_no_senders (struct port_info *pi,
			   mach_port_mscount_t mscount)
{
  return ports_do_mach_notify_no_senders (pi, mscount);
}

kern_return_t
do_mach_notify_port_deleted (struct port_info *pi,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_msg_accepted (struct port_info *pi,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_port_destroyed (struct port_info *pi,
			       mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_send_once (struct port_info *pi)
{
  return 0;
}
