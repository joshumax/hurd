/* Message port manipulations
   Copyright (C) 1994, 1995, 1996, 1999, 2001 Free Software Foundation

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
#include <hurd/startup.h>
#include <assert-backtrace.h>
#include <stdlib.h>
#include <stdio.h>

/* Check to see if process P is blocked trying to get the message
   port of process AVAILP; if so, return its call.  */
void
check_message_return (struct proc *p, void *availpaddr)
{
  if (p->p_msgportwait)
    {
      pthread_cond_broadcast (&p->p_wakeup);
      p->p_msgportwait = 0;
    }
}

/* Register ourselves with statup. */
static void *
tickle_statup (void *statupport)
{
  startup_essential_task ((mach_port_t) statupport, mach_task_self (),
			  MACH_PORT_NULL, "proc", _hurd_host_priv);
  return NULL;
}

error_t
S_proc_setmsgport (struct proc *p,
		   mach_port_t reply, mach_msg_type_name_t replytype,
		   mach_port_t msgport,
		   mach_port_t *oldmsgport,
		   mach_msg_type_name_t *oldmsgport_type)
{
  if (!p)
    return EOPNOTSUPP;

  *oldmsgport = p->p_msgport;
  *oldmsgport_type = MACH_MSG_TYPE_MOVE_SEND;

  p->p_msgport = msgport;
  p->p_deadmsg = 0;
  if (p->p_checkmsghangs)
    prociterate (check_message_return, p);
  p->p_checkmsghangs = 0;

  if (p == startup_proc && startup_fallback)
    {
    /* Statup is single threaded, so we can't delay our reply for
       the essential task RPC; spawn a thread to do it. */
      pthread_t thread;
      error_t err;
      err = pthread_create (&thread, NULL, tickle_statup,
			    (void*) (uintptr_t) msgport);
      if (!err)
	pthread_detach (thread);
      else
	{
	  errno = err;
	  perror ("pthread_create");
	}
    }
      
  return 0;
}

/* Check to see if process P is blocked trying to get the message port of 
   process DYINGP; if so, wake it up. */
void
check_message_dying (struct proc *p, struct proc *dyingp)
{
  if (p->p_msgportwait)
    {
      pthread_cond_broadcast (&p->p_wakeup);
      p->p_msgportwait = 0;
    }
}

/* Check if the message port of process P has died.  Return nonzero if
   this has indeed happened.  */
int
check_msgport_death (struct proc *p)
{
  /* Only check if the message port passed away, if we know that it
     was ever alive.  */
  if (p->p_msgport != MACH_PORT_NULL)
    {
      mach_port_type_t type;
      error_t err;
      
      err = mach_port_type (mach_task_self (), p->p_msgport, &type);
      if (err || (type & MACH_PORT_TYPE_DEAD_NAME))
	{
	  /* The port appears to be dead; throw it away. */
	  mach_port_deallocate (mach_task_self (), p->p_msgport);
	  p->p_msgport = MACH_PORT_NULL;
	  p->p_deadmsg = 1;
	  return 1;
	}
    }

  return 0;
}

error_t
S_proc_getmsgport (struct proc *callerp,
		   mach_port_t reply_port,
		   mach_msg_type_name_t reply_port_type,
		   pid_t pid,
		   mach_port_t *msgport,
                   mach_msg_type_name_t *msgport_type)
{
  int cancel;
  struct proc *p;

  if (!callerp)
    return EOPNOTSUPP;

  p = pid_find_allow_zombie (pid);

  if (namespace_is_subprocess (p))
    {
      /* Relay it to the Subhurd's proc server (if any).  */
      error_t err;
      pid_t pid_sub;

      /* Release global lock while talking to the other proc server.  */
      pthread_mutex_unlock (&global_lock);

      err = proc_task2pid (p->p_task_namespace, p->p_task, &pid_sub);
      if (! err)
        err = proc_getmsgport (p->p_task_namespace, pid_sub, msgport);

      pthread_mutex_lock (&global_lock);

      if (! err)
	{
	  *msgport_type = MACH_MSG_TYPE_MOVE_SEND;
	  return 0;
	}

      /* Fallback.  */
    }

 restart:
  while (p && p->p_deadmsg && !p->p_dead)
    {
      callerp->p_msgportwait = 1;
      p->p_checkmsghangs = 1;
      cancel = pthread_hurd_cond_wait_np (&callerp->p_wakeup, &global_lock);
      if (callerp->p_dead)
	return EOPNOTSUPP;
      if (cancel)
	return EINTR;

      /* Refetch P in case it went away while we were waiting.  */
      p = pid_find_allow_zombie (pid);
    }

  if (!p)
    return ESRCH;

  if (check_msgport_death (p))
    goto restart;

  *msgport_type = MACH_MSG_TYPE_COPY_SEND;
  *msgport = p->p_msgport;

  return 0;
}
