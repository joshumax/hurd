/* Implementation of wait
   Copyright (C) 1994, 1995 Free Software Foundation

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
#include "process_reply_U.h"
#include "ourmsg_U.h"
#include "interrupt_S.h"

#include <mach/mig_errors.h>

struct zombie 
{
  struct zombie *next;
  pid_t pid, pgrp;
  struct proc *parent;
  int exit_status;
  struct rusage ru;
};

static struct zombie *zombie_list;


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

/* Return nonzero iff PARENT is waiting for PID/PGRP.  */
static inline int
waiting_parent_cares (struct proc *parent, pid_t pid, pid_t pgrp)
{
  struct wait_c *w = &parent->p_continuation.wait_c;
  return waiter_cares (w->pid, parent->p_pgrp->pg_pgid, pid, pgrp);
}


/* A process is dying.  Send SIGCHLD to the parent.  Check if the parent is
   waiting for us to exit; if so wake it up, otherwise, enter us as a
   zombie.  */
void
alert_parent (struct proc *p)
{
  struct zombie *z;

  send_signal (p->p_parent->p_msgport, SIGCHLD, p->p_parent->p_task);

  if (!p->p_exiting)
    p->p_status = W_EXITCODE (0, SIGKILL);

  if (p->p_parent->p_waiting)
    {
      struct wait_c *w = &p->p_parent->p_continuation.wait_c;
      if (waiting_parent_cares (p->p_parent, p->p_pid, p->p_pgrp->pg_pgid))
	{
	  struct rusage ru;
      
	  bzero (&ru, sizeof (struct rusage));
	  proc_wait_reply (w->reply_port, w->reply_port_type, 0,
			   p->p_status, ru, p->p_pid);
	  p->p_parent->p_waiting = 0;
	  return;
	}
    }

  z = malloc (sizeof (struct zombie));
      
  z->pid = p->p_pid;
  z->pgrp = p->p_pgrp->pg_pgid;
  z->parent = p->p_parent;
  z->exit_status = p->p_status;
  bzero (&z->ru, sizeof (struct rusage));
  z->next = zombie_list;
  zombie_list = z;
}

/* Process P is exiting.  Find all the zombies who claim P as their parent
   and make them claim startup_proc as their parent; then wake it
   up if appropriate. */
void
reparent_zombies (struct proc *p)
{
  struct zombie *z, *prevz;
  struct wait_c *w = &startup_proc->p_continuation.wait_c;
  int initwoken = 0, initsignalled = 0;
  
  for (z = zombie_list, prevz = 0; z; prevz = z, z = z->next)
    {
      if (z->parent != p)
	continue;
      z->parent = startup_proc;

      if (!initsignalled)
	{
	  send_signal (startup_proc->p_msgport, SIGCHLD, startup_proc->p_task);
	  initsignalled = 1;
	}

      if (initwoken || !startup_proc->p_waiting)
	continue;

      if (waiting_parent_cares (startup_proc, z->pid, z->pgrp))
	{
	  proc_wait_reply (w->reply_port, w->reply_port_type, 0,
			   z->exit_status, z->ru, z->pid);
	  startup_proc->p_waiting = 0;
	  (prevz ? prevz->next : zombie_list) = z->next;
	  free (z);
	  initwoken = 1;
	}
    }
}

/* Cause the pending wait operation of process P to immediately
   return. */
void
abort_wait (struct proc *p)
{
  struct wait_c *w = &p->p_continuation.wait_c;
  struct rusage ru;

  proc_wait_reply (w->reply_port, w->reply_port_type, EINTR,
		   0, ru, 0);
  p->p_waiting = 0;
}

/* Implement proc_wait as described in <hurd/proc.defs>. */
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
  struct wait_c *w;
  struct zombie *z, *prevz;
  
  for (z = zombie_list, prevz = 0; z; prevz = z, z = z->next)
    {
      if (z->parent == p && waiter_cares (pid, p->p_pgrp->pg_pgid,
					  z->pid, z->pgrp))
	{
	  *status = z->exit_status;
	  bzero (ru, sizeof (struct rusage));
	  *pid_status = z->pid;
	  (prevz ? prevz->next : zombie_list) = z->next;
	  free (z);
	  return 0;
	}
    }

  /* See if we can satisfy the request with a stopped
     child; also check for invalid arguments here. */
  if (!p->p_ochild) 
    return ECHILD;
  
  if (pid > 0)
    {
      struct proc *child = pid_find (pid);
      if (!child || child->p_parent != p)
	return ECHILD;
      if (child->p_stopped && !child->p_waited
	  && ((options & WUNTRACED) || child->p_traced))
	{
	  child->p_waited = 1;
	  *status = child->p_status;
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
	    if (child->p_stopped && !child->p_waited
		&& ((options & WUNTRACED) || child->p_traced))
	      {
		child->p_waited = 1;
		*status = child->p_status;
		bzero (ru, sizeof (struct rusage));
		*pid_status = child->p_pid;
		return 0;
	      }
	  }

      if (!had_a_match)
	return ECHILD;
    }
  
  if (options & WNOHANG)
    return EWOULDBLOCK;

  if (p->p_waiting)
    return EBUSY;
  
  p->p_waiting = 1;
  w = &p->p_continuation.wait_c;
  w->reply_port = reply_port;
  w->reply_port_type = reply_port_type;
  w->pid = pid;
  w->options = options;
  return MIG_NO_REPLY;
}

/* Implement proc_mark_stop as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_stop (struct proc *p,
	       int signo)
{
  p->p_stopped = 1;
  p->p_status = W_STOPCODE (signo);
  p->p_waited = 0;
  
  if (p->p_parent->p_waiting)
    {
      struct wait_c *w = &p->p_parent->p_continuation.wait_c;
      if (((w->options & WUNTRACED) || p->p_traced)
	  && waiting_parent_cares (p->p_parent, p->p_pid, p->p_pgrp->pg_pgid))
	{
	  struct rusage ru;
	  bzero (&ru, sizeof (struct rusage));
	  proc_wait_reply (w->reply_port, w->reply_port_type, 0,
			   p->p_status, ru, p->p_pid);
	  p->p_parent->p_waiting = 0;
	  p->p_waited = 1;
	}
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
  p->p_stopped = 0;
  return 0;
}

/* Implement proc_mark_traced as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_traced (struct proc *p)
{
  p->p_traced = 1;
  return 0;
}

/* Implement proc_mark_nostopchild as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mod_stopchild (struct proc *p,
		      int value)
{
  /* VALUE is nonzero if we should send SIGCHLD.  */
  p->p_nostopcld = ! value;
  return 0;
}

/* Return 1 if pid is in use by a zombie. */
int
zombie_check_pid (pid_t pid)
{
  struct zombie *z;
  for (z = zombie_list; z; z = z->next)
    if (z->pid == pid || -z->pid == pid
	|| z->pgrp == pid || -z->pgrp == pid)
      return 1;
  return 0;
}

/* Implement interrupt_operation as described in <hurd/interrupt.defs>. */
kern_return_t
S_interrupt_operation (mach_port_t object)
{
  struct proc *p = reqport_find (object);
  
  if (!p)
    return EOPNOTSUPP;
  
  if (p->p_waiting)
    abort_wait (p);
  if (p->p_msgportwait)
    abort_getmsgport (p);

  return 0;
}
