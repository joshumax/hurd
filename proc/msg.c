/* Message port manipulations
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

#include <mach.h>
#include <hurd.h>
#include "proc.h"
#include "process_reply.h"
#include <assert.h>
#include <stdlib.h>

/* Check to see if process P is blocked trying to get the message
   port of process AVAILP; if so, return its call.  */
void
check_message_return (struct proc *p, void *availpaddr)
{
  struct proc *availp = availpaddr;
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  
  if (p->p_msgportwait && c->msgp == availp)
    {
      proc_getmsgport_reply (c->reply_port, c->reply_port_type,
			     0, availp->p_msgport);
      c->msgp = 0;
      p->p_msgportwait = 0;
    }
}

error_t
S_proc_setmsgport (struct proc *p,
		 mach_port_t msgport,
		 mach_port_t *oldmsgport)
{
  *oldmsgport = p->p_msgport;
  p->p_msgport = msgport;
  p->p_deadmsg = 0;
  if (p->p_checkmsghangs)
    prociterate (check_message_return, p);
  p->p_checkmsghangs = 0;

  if (p == startup_proc)
    startup_essential_task (msgport, mach_task_self (), MACH_PORT_NULL,
			    "proc", master_host_port);
      
  return 0;
}

/* Check to see if process P is blocked trying to get the message port of 
   process DYINGP; if so, return its call with ESRCH. */
void
check_message_dying (struct proc *p, struct proc *dyingp)
{
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  
  if (p->p_msgportwait && c->msgp == dyingp)
    {
      proc_getmsgport_reply (c->reply_port, c->reply_port_type, ESRCH,
			     MACH_PORT_NULL);
      c->msgp = 0;
      p->p_msgportwait = 0;
    }
}

/* Cause a pending proc_getmsgport operation to immediately return */
void
abort_getmsgport (struct proc *p)
{
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  
  proc_getmsgport_reply (c->reply_port, c->reply_port_type, EINTR,
			 MACH_PORT_NULL);
  c->msgp = 0;
  p->p_msgportwait = 0;
}

error_t
S_proc_getmsgport (struct proc *callerp,
		   mach_port_t reply_port,
		   mach_msg_type_name_t reply_port_type,
		   pid_t pid,
		   mach_port_t *msgport)
{
  struct proc *p = pid_find (pid);

  if (!p)
    return ESRCH;
  
  if (p->p_deadmsg)
    {
      struct getmsgport_c *c = &callerp->p_continuation.getmsgport_c;
      if (callerp->p_msgportwait)
	return EBUSY;
      c->reply_port = reply_port;
      c->reply_port_type = reply_port_type;
      c->msgp = p;
      p->p_checkmsghangs = 1;
      callerp->p_msgportwait = 1;
      return MIG_NO_REPLY;
    }
  
  *msgport = p->p_msgport;
  return 0;
}

void
message_port_dead (struct proc *p)
{
  mach_port_deallocate (mach_task_self (), p->p_msgport);
  p->p_msgport = MACH_PORT_NULL;
  p->p_deadmsg = 1;
}

