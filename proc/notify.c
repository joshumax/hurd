/* Handle notifications
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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
   port.  */
kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t deadport)
{
  struct proc *p = reqport_find (notify);
  
  if (!p)
    return EOPNOTSUPP;
  
  if (p->p_reqport == deadport)
    {
      message_port_dead (p);
      return 0;
    }
  else if (p->p_task == deadport)
    {
      process_has_exited (p);
      return 0;
    }
  else
    return EINVAL;
}

/* We get no-senders notifications on exception ports that we 
   handle through proc_handle_exceptions. */
kern_return_t
do_mach_notify_no_senders (mach_port_t notify,
			   mach_port_mscount_t mscount)
{
  struct exc *e = exc_find (notify);
  if (!e)
    return EOPNOTSUPP;
  
  remove_exc_from_hash (e);
  mach_port_mod_refs (mach_task_self (), e->excport,
		      MACH_PORT_RIGHT_RECEIVE, -1);
  mach_port_deallocate (mach_task_self (), e->forwardport);
  if (e->replyport != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), e->replyport);
  free (e);
  return 0;
}

kern_return_t
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t name)
{
  return 0;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return 0;
}
