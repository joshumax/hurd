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
check_message_return (struct proc *p, struct proc *availp)
{
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  struct proc *cp;
  mach_port_t *msgports;
  int i;
  
  if (p->p_msgportwait && c->waiting == availp)
    {
      msgports = alloca (sizeof (mach_port_t) * c->nprocs);
      for (i = 0; i < c->nprocs; i++)
	{
	  cp = c->procs[i];
	  if (cp->p_deadmsg)
	    {
	      assert (cp != availp);
	      c->waiting = cp;
	      cp->p_checkmsghangs = 1;
	      return;
	    }
	  msgports[i] = cp->p_msgport;
	}
      p->p_msgportwait = 0;
      free (c->procs);
      proc_getmsgport_reply (c->reply_port, c->reply_port_type, 0,
			     msgports, MACH_MSG_TYPE_COPY_SEND,
			     c->nprocs);
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
      
  return 0;
}

/* Check to see if process P is blocked trying to get the message port of 
   process DYINGP; if so, return its call with ESRCH. */
void
check_message_dying (struct proc *p, struct proc *dyingp)
{
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  int i;
  
  if (p->p_msgportwait)
    for (i = 0; i < c->nprocs; i++)
      if (c->procs[i] == dyingp)
	{
	  proc_getmsgport_reply (c->reply_port, c->reply_port_type, ESRCH,
				 0, MACH_MSG_TYPE_COPY_SEND, 0);
	  free (c->procs);
	  p->p_msgportwait = 0;
	}
}

/* Cause a pending proc_getmsgport operation to immediately return */
void
abort_getmsgport (struct proc *p)
{
  struct getmsgport_c *c = &p->p_continuation.getmsgport_c;
  
  proc_getmsgport_reply (c->reply_port, c->reply_port_type, EINTR,
			 0, MACH_MSG_TYPE_COPY_SEND, 0);
  free (c->procs);
  p->p_msgportwait = 0;
}

error_t
S_proc_getmsgport (struct proc *callerp,
		   mach_port_t reply_port,
		   mach_msg_type_name_t reply_port_type,
		   int *pids,
		   u_int pidslen,
		   mach_port_t **msgports,
		   mach_msg_type_name_t *msgportname,
		   u_int *msgportlen)
{
  struct proc *p;
  int i;
  struct proc **procs = malloc (sizeof (struct proc *) * pidslen);

  for (i = 0; i < pidslen; i++)
    {
      p = pid_find (pids[i]);
      if (!p)
	{
	  free (procs);
	  return ESRCH;
	}
      procs[i] = p;
    }
  
  for (i = 0; i < pidslen; i++)
    if (procs[i]->p_deadmsg)
      {
	struct getmsgport_c *c = &callerp->p_continuation.getmsgport_c;
	if (callerp->p_msgportwait)
	  {
	    free (procs);
	    return EBUSY;
	  }
	c->reply_port = reply_port;
	c->reply_port_type = reply_port_type;
	c->waiting = procs[i];
	c->procs = procs;
	c->nprocs = pidslen;
	procs[i]->p_checkmsghangs = 1;
	callerp->p_msgportwait = 1;
	break;
      }
  
  if (callerp->p_msgportwait)
    return MIG_NO_REPLY;
  else
    {
      if (pidslen * sizeof (mach_port_t) > *msgportlen)
	vm_allocate (mach_task_self (), (vm_address_t *) msgports,
		     pidslen * sizeof (mach_port_t), 1);
      for (i = 0; i < pidslen; i++)
	(*msgports)[i] = procs[i]->p_msgport;
      free (procs);
      return 0;
    }
}

void
message_port_dead (struct proc *p)
{
  mach_port_deallocate (mach_task_self (), p->p_msgport);
  p->p_msgport = MACH_PORT_NULL;
  p->p_deadmsg = 1;
}

