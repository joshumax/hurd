/* Implementation of wait
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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
#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <sys/resource.h>

#include "proc.h"

#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

#include "process_S.h"
#include <mach/mig_errors.h>

/* Return nonzero if a `waitpid' on WAIT_PID by a process
   in MYPGRP cares about the death of PID/PGRP.  */
static inline int
waiter_cares (pid_t wait_pid, pid_t mypgrp,
	      pid_t pid, pid_t pgrp)
{
  return (wait_pid == pid ||
	  wait_pid == -pgrp ||
	  wait_pid == WAIT_ANY ||
	  (wait_pid == WAIT_MYPGRP && pgrp == mypgrp));
}

/* A process is dying.  Send SIGCHLD to the parent.  Wake the parent
if it is waiting for us to exit. */
void
alert_parent (struct proc *p)
{
  send_signal (p->p_parent->p_msgport, SIGCHLD, p->p_parent->p_task);

  if (!p->p_exiting)
    p->p_status = W_EXITCODE (0, SIGKILL);

  if (p->p_parent->p_waiting)
    {
      condition_broadcast (&p->p_parent->p_wakeup);
      p->p_parent->p_waiting = 0;
    }
}

kern_return_t
S_proc_wait (struct proc *p,
	     mach_port_t reply_port,
	     mach_msg_type_name_t reply_port_type,
	     pid_t pid,
	     int options,
	     int *status,
	     struct rusage *ru,
	     pid_t *pid_status)
{
  int cancel;
  
  int child_ready (struct proc *child)
    {
      if (child->p_waited)
	return 0;
      if (child->p_dead)
	return 1;
      if (!child->p_stopped)
	return 0;
      if (child->p_traced || (options & WUNTRACED))
	return 1;
      return 0;
    }

  if (!p)
    return EOPNOTSUPP;
  
 start_over:
  /* See if we can satisfy the request with a stopped
     child; also check for invalid arguments here. */
  if (!p->p_ochild) 
    return ECHILD;
  
  if (pid > 0)
    {
      struct proc *child = pid_find_allow_zombie (pid);
      if (!child || child->p_parent != p)
	return ECHILD;
      if (child_ready (child))
	{
	  child->p_waited = 1;
	  *status = child->p_status;
	  if (child->p_dead)
	    complete_exit (child);
	  bzero (ru, sizeof (struct rusage));
	  *pid_status = pid;
	  return 0;
	}
    }
  else
    {
      struct proc *child;
      int had_a_match = pid == 0;
      
      for (child = p->p_ochild; child; child = child->p_sib)
	if (waiter_cares (pid, p->p_pgrp->pg_pgid,
			  child->p_pid, child->p_pgrp->pg_pgid))
	  {
	    had_a_match = 1;
	    if (child_ready (child))
	      {
		child->p_waited = 1;
		*status = child->p_status;
		*pid_status = child->p_pid;
		if (child->p_dead)
		  complete_exit (child);
		bzero (ru, sizeof (struct rusage));
		return 0;
	      }
	  }

      if (!had_a_match)
	return ECHILD;
    }
  
  if (options & WNOHANG)
    return EWOULDBLOCK;

  p->p_waiting = 1;
  cancel = hurd_condition_wait (&p->p_wakeup, &global_lock);
  if (p->p_dead)
    return EOPNOTSUPP;
  if (cancel)
    return EINTR;
  goto start_over;
}

/* Implement proc_mark_stop as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_stop (struct proc *p,
	       int signo)
{
  if (!p)
    return EOPNOTSUPP;

  p->p_stopped = 1;
  p->p_status = W_STOPCODE (signo);
  p->p_waited = 0;
  
  if (p->p_parent->p_waiting)
    {
      condition_broadcast (&p->p_parent->p_wakeup);
      p->p_parent->p_waiting = 0;
    }
  
  if (!p->p_parent->p_nostopcld)
    send_signal (p->p_parent->p_msgport, SIGCHLD, p->p_parent->p_task);

  return 0;
}

/* Implement proc_mark_exit as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_exit (struct proc *p,
		int status)
{
  if (!p)
    return EOPNOTSUPP;
  
  if (WIFSTOPPED (status))
    return EINVAL;
  
  p->p_exiting = 1;
  p->p_status = status;
  return 0;
}

/* Implement proc_mark_cont as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_cont (struct proc *p)
{
  if (!p)
    return EOPNOTSUPP;
  p->p_stopped = 0;
  return 0;
}

/* Implement proc_mark_traced as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_traced (struct proc *p)
{
  if (!p)
    return EOPNOTSUPP;
  p->p_traced = 1;
  return 0;
}

/* Implement proc_mark_nostopchild as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mod_stopchild (struct proc *p,
		      int value)
{
  if (!p)
    return EOPNOTSUPP;
  /* VALUE is nonzero if we should send SIGCHLD.  */
  p->p_nostopcld = ! value;
  return 0;
}

