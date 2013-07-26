/* Process management
   Copyright (C) 1992,93,94,95,96,99,2000,01,02 Free Software Foundation, Inc.

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
#include <sys/mman.h>
#include <errno.h>
#include <hurd/hurd_types.h>
#include <stdlib.h>
#include <string.h>
#include <mach/notify.h>
#include <sys/wait.h>
#include <mach/mig_errors.h>
#include <sys/resource.h>
#include <hurd/auth.h>
#include <assert.h>
#include <pids.h>

#include "proc.h"
#include "process_S.h"
#include "mutated_ourmsg_U.h"
#include "proc_exc_S.h"
#include "proc_exc_U.h"
#include <hurd/signal.h>

/* Create a new id structure with the given genuine uids and gids. */
static inline struct ids *
make_ids (const uid_t *uids, size_t nuids)
{
  struct ids *i;

  i = malloc (sizeof (struct ids) + sizeof (uid_t) * nuids);;
  if (! i)
    return NULL;

  i->i_nuids = nuids;
  i->i_refcnt = 1;

  memcpy (&i->i_uids, uids, sizeof (uid_t) * nuids);
  return i;
}

static inline void
ids_ref (struct ids *i)
{
  i->i_refcnt ++;
}

/* Free an id structure. */
static inline void
ids_rele (struct ids *i)
{
  i->i_refcnt --;
  if (i->i_refcnt == 0)
    free (i);
}

/* Tell if process P has uid UID, or has root.  */
int
check_uid (struct proc *p, uid_t uid)
{
  int i;
  for (i = 0; i < p->p_id->i_nuids; i++)
    if (p->p_id->i_uids[i] == uid || p->p_id->i_uids[i] == 0)
      return 1;
  return 0;
}

/* Implement proc_reathenticate as described in <hurd/process.defs>. */
kern_return_t
S_proc_reauthenticate (struct proc *p, mach_port_t rendport)
{
  error_t err;
  uid_t gubuf[50], aubuf[50], ggbuf[50], agbuf[50];
  uid_t *gen_uids, *aux_uids, *gen_gids, *aux_gids;
  size_t ngen_uids, naux_uids, ngen_gids, naux_gids;

  if (!p)
    return EOPNOTSUPP;

  gen_uids = gubuf;
  aux_uids = aubuf;
  gen_gids = ggbuf;
  aux_gids = agbuf;

  ngen_uids = sizeof (gubuf) / sizeof (uid_t);
  naux_uids = sizeof (aubuf) / sizeof (uid_t);
  ngen_gids = sizeof (ggbuf) / sizeof (uid_t);
  naux_gids = sizeof (agbuf) / sizeof (uid_t);

  /* Release the global lock while blocking on the auth server and client.  */
  pthread_mutex_unlock (&global_lock);
  err = auth_server_authenticate (authserver,
				  rendport, MACH_MSG_TYPE_COPY_SEND,
				  MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND,
				  &gen_uids, &ngen_uids,
				  &aux_uids, &naux_uids,
				  &gen_gids, &ngen_gids,
				  &aux_gids, &naux_gids);
  pthread_mutex_lock (&global_lock);

  if (err)
    return err;

  if (p->p_dead)
    /* The process died while we had the lock released.
       Its p_id field is no longer valid and we shouldn't touch it.  */
    err = EAGAIN;
  else
    {
      ids_rele (p->p_id);
      p->p_id = make_ids (gen_uids, ngen_uids);
      if (! p->p_id)
	err = ENOMEM;
    }

  if (gen_uids != gubuf)
    munmap (gen_uids, ngen_uids * sizeof (uid_t));
  if (aux_uids != aubuf)
    munmap (aux_uids, naux_uids * sizeof (uid_t));
  if (gen_gids != ggbuf)
    munmap (gen_gids, ngen_gids * sizeof (uid_t));
  if (aux_gids != agbuf)
    munmap (aux_gids, naux_gids * sizeof (uid_t));

  if (!err)
    mach_port_deallocate (mach_task_self (), rendport);
  return err;
}

/* Implement proc_child as described in <hurd/process.defs>. */
kern_return_t
S_proc_child (struct proc *parentp,
	      task_t childt)
{
  struct proc *childp;

  if (!parentp)
    return EOPNOTSUPP;

  childp = task_find (childt);
  if (!childp)
    return ESRCH;

  if (childp->p_parentset)
    return EBUSY;

  mach_port_deallocate (mach_task_self (), childt);

  /* Process identification.
     Leave p_task and p_pid alone; all the rest comes from the
     new parent. */

  if (!--childp->p_login->l_refcnt)
    free (childp->p_login);
  childp->p_login = parentp->p_login;
  childp->p_login->l_refcnt++;

  childp->p_owner = parentp->p_owner;
  childp->p_noowner = parentp->p_noowner;

  ids_rele (childp->p_id);
  ids_ref (parentp->p_id);
  childp->p_id = parentp->p_id;

  /* Process hierarchy.  Remove from our current location
     and place us under our new parent.  Sanity check to make sure
     parent is currently init. */
  assert (childp->p_parent == startup_proc);
  if (childp->p_sib)
    childp->p_sib->p_prevsib = childp->p_prevsib;
  *childp->p_prevsib = childp->p_sib;

  childp->p_parent = parentp;
  childp->p_sib = parentp->p_ochild;
  childp->p_prevsib = &parentp->p_ochild;
  if (parentp->p_ochild)
    parentp->p_ochild->p_prevsib = &childp->p_sib;
  parentp->p_ochild = childp;

  /* Process group structure. */
  if (childp->p_pgrp != parentp->p_pgrp)
    {
      leave_pgrp (childp);
      childp->p_pgrp = parentp->p_pgrp;
      join_pgrp (childp);
      /* Not necessary to call newids ourself because join_pgrp does
	 it for us. */
    }
  else if (childp->p_msgport != MACH_PORT_NULL)
    nowait_msg_proc_newids (childp->p_msgport, childp->p_task,
			    childp->p_parent->p_pid, childp->p_pgrp->pg_pgid,
			    !childp->p_pgrp->pg_orphcnt);
  childp->p_parentset = 1;
  return 0;
}

/* Implement proc_reassign as described in <hurd/process.defs>. */
kern_return_t
S_proc_reassign (struct proc *p,
		 task_t newt)
{
  struct proc *stubp;

  if (!p)
    return EOPNOTSUPP;

  stubp = task_find (newt);
  if (!stubp)
    return ESRCH;

  if (stubp == p)
    return EINVAL;

  mach_port_deallocate (mach_task_self (), newt);

  remove_proc_from_hash (p);

  task_terminate (p->p_task);
  mach_port_destroy (mach_task_self (), p->p_task);
  p->p_task = stubp->p_task;

  /* For security, we need to use the request port from STUBP */
  ports_transfer_right (p, stubp);

  /* Enqueued messages might refer to the old task port, so
     destroy them. */
  if (p->p_msgport != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), p->p_msgport);
      p->p_msgport = MACH_PORT_NULL;
      p->p_deadmsg = 1;
    }

  /* These two are image dependent. */
  p->p_argv = stubp->p_argv;
  p->p_envp = stubp->p_envp;

  /* Destroy stubp */
  stubp->p_task = MACH_PORT_NULL;/* block deallocation */
  process_has_exited (stubp);
  stubp->p_waited = 1;		/* fake out complete_exit */
  complete_exit (stubp);

  add_proc_to_hash (p);

  return 0;
}

/* Implement proc_setowner as described in <hurd/process.defs>. */
kern_return_t
S_proc_setowner (struct proc *p,
		 uid_t owner,
		 int clear)
{
  if (!p)
    return EOPNOTSUPP;

  if (clear)
    p->p_noowner = 1;
  else
    {
      if (! check_uid (p, owner))
	return EPERM;

      p->p_owner = owner;
      p->p_noowner = 0;
    }

  return 0;
}

/* Implement proc_getpids as described in <hurd/process.defs>. */
kern_return_t
S_proc_getpids (struct proc *p,
		pid_t *pid,
		pid_t *ppid,
		int *orphaned)
{
  if (!p)
    return EOPNOTSUPP;
  *pid = p->p_pid;
  *ppid = p->p_parent->p_pid;
  *orphaned = !p->p_pgrp->pg_orphcnt;
  return 0;
}

/* Implement proc_set_arg_locations as described in <hurd/process.defs>. */
kern_return_t
S_proc_set_arg_locations (struct proc *p,
			  vm_address_t argv,
			  vm_address_t envp)
{
  if (!p)
    return EOPNOTSUPP;
  p->p_argv = argv;
  p->p_envp = envp;
  return 0;
}

/* Implement proc_get_arg_locations as described in <hurd/process.defs>. */
kern_return_t
S_proc_get_arg_locations (struct proc *p,
			  vm_address_t *argv,
			  vm_address_t *envp)
{
  *argv = p->p_argv;
  *envp = p->p_envp;
  return 0;
}

/* Implement proc_dostop as described in <hurd/process.defs>. */
kern_return_t
S_proc_dostop (struct proc *p,
	       thread_t contthread)
{
  thread_t threadbuf[2], *threads = threadbuf;
  size_t nthreads = sizeof (threadbuf) / sizeof (thread_t);
  int i;
  error_t err;

  if (!p)
    return EOPNOTSUPP;

  err = task_suspend (p->p_task);
  if (err)
    return err;
  err = task_threads (p->p_task, &threads, &nthreads);
  if (err)
    {
      task_resume (p->p_task);
      return err;
    }
  /* We can not compare the thread ports with CONTTHREAD, as CONTTHREAD
     might be a proxy port (for example in rpctrace).  For this reason
     we suspend all threads and then resume  CONTTHREAD.  */
  for (i = 0; i < nthreads; i++)
    {
      if (threads[i] != contthread)
	thread_suspend (threads[i]);
      mach_port_deallocate (mach_task_self (), threads[i]);
    }
  if (threads != threadbuf)
    munmap (threads, nthreads * sizeof (thread_t));
  err = task_resume (p->p_task);
  if (err)
    return err;

  mach_port_deallocate (mach_task_self (), contthread);
  return 0;
}

/* Clean state of E before it is deallocated */
void
exc_clean (void *arg)
{
  struct exc *e = arg;
  mach_port_deallocate (mach_task_self (), e->forwardport);
}

/* Implement proc_handle_exceptions as described in <hurd/process.defs>. */
kern_return_t
S_proc_handle_exceptions (struct proc *p,
			  mach_port_t msgport,
			  mach_port_t forwardport,
			  int flavor,
			  thread_state_t new_state,
			  mach_msg_type_number_t statecnt)
{
  struct exc *e;
  error_t err;

  /* No need to check P here; we don't use it. */

  err = ports_import_port (exc_class, proc_bucket, msgport,
			   (sizeof (struct exc)
			    + (statecnt * sizeof (natural_t))), &e);
  if (err)
    return err;

  e->forwardport = forwardport;
  e->flavor = flavor;
  e->statecnt = statecnt;
  memcpy (e->thread_state, new_state, statecnt * sizeof (natural_t));
  ports_port_deref (e);
  return 0;
}

/* Called on exception ports provided to proc_handle_exceptions.  Do
   the thread_set_state requested by proc_handle_exceptions and then
   send an exception_raise message as requested. */
kern_return_t
S_proc_exception_raise (mach_port_t excport,
			mach_port_t reply,
			mach_msg_type_name_t reply_type,
			mach_port_t thread,
			mach_port_t task,
			integer_t exception,
			integer_t code,
			integer_t subcode)
{
  error_t err;
  struct proc *p;
  struct exc *e = ports_lookup_port (proc_bucket, excport, exc_class);
  if (!e)
    return EOPNOTSUPP;

  p = task_find (task);
  if (! p)
    {
      /* Bogus RPC.  */
      ports_port_deref (e);
      return EINVAL;
    }

  /* Try to forward the message.  */
  err = proc_exception_raise (e->forwardport,
			      reply, reply_type, MACH_SEND_NOTIFY,
			      thread, task, exception, code, subcode);

  switch (err)
    {
      struct hurd_signal_detail hsd;
      int signo;

    case 0:
      /* We have successfully forwarded the exception message.  Now reset
	 the faulting thread's state to run its recovery code, which should
	 dequeue that message.  */
      err = thread_set_state (thread, e->flavor, e->thread_state, e->statecnt);
      ports_port_deref (e);
      mach_port_deallocate (mach_task_self (), thread);
      mach_port_deallocate (mach_task_self (), task);
      return MIG_NO_REPLY;

    default:
      /* Some unexpected error in forwarding the message.  */
      /* FALLTHROUGH */

    case MACH_SEND_NOTIFY_IN_PROGRESS:
      /* The port's queue is full; this means the thread didn't receive
	 the exception message we forwarded last time it faulted.
	 Declare that signal thread hopeless and the task crashed.  */

      /* Translate the exception code into a signal number
	 and mark the process as having died that way.  */
      hsd.exc = exception;
      hsd.exc_code = code;
      hsd.exc_subcode = subcode;
      _hurd_exception2signal (&hsd, &signo);
      p->p_exiting = 1;
      p->p_status = W_EXITCODE (0, signo);
      p->p_sigcode = hsd.code;

      /* Nuke the task; we will get a notification message and report that
	 it died with SIGNO.  */
      task_terminate (task);
      ports_port_deref (e);

      /* In the MACH_SEND_NOTIFY_IN_PROGRESS case, the kernel did a
	 pseudo-receive of the RPC request message that may have added user
	 refs to these send rights.  But we have lost track because the MiG
	 stub did not save the message buffer that was modified by the
	 pseudo-receive.

	 Fortunately, we can be sure that we don't need the THREAD send
	 right for anything since this task is now dead; there would be a
	 potential race here with another exception_raise message arriving
	 with the same thread, but we expect that this won't happen since
	 the thread will still be waiting for our reply.  XXX We have no
	 secure knowledge that this is really from the kernel, so a
	 malicious user could confuse us and induce a race where we clobber
	 another port put on the THREAD name after the destroy; also, a
	 user just doing thread_set_state et al could arrange that we get a
	 second legitimate exception_raise for the same thread and have the
	 first race mentioned above!

	 There are all manner of race problems if we destroy the TASK
	 right.  Fortunately, since we've terminated the task we know that
	 we will shortly be getting a dead-name notifiction and that will
	 call mach_port_destroy in TASK when it is safe to do so.  */

      mach_port_destroy (mach_task_self (), thread);

      return MIG_NO_REPLY;
    }

}

/* Implement proc_getallpids as described in <hurd/process.defs>. */
kern_return_t
S_proc_getallpids (struct proc *p,
		   pid_t **pids,
		   size_t *pidslen)
{
  int nprocs;
  pid_t *loc;

  void count_up (struct proc *p, void *counter)
    {
      ++*(int *)counter;
    }
  void store_pid (struct proc *p, void *loc)
    {
      *(*(pid_t **)loc)++ = p->p_pid;
    }

  /* No need to check P here; we don't use it. */

  add_tasks (0);

  nprocs = 0;
  prociterate (count_up, &nprocs);

  if (nprocs > *pidslen)
    {
      *pids = mmap (0, nprocs * sizeof (pid_t), PROT_READ|PROT_WRITE,
		    MAP_ANON, 0, 0);
      if (*pids == MAP_FAILED)
        return ENOMEM;
    }

  loc = *pids;
  prociterate (store_pid, &loc);

  *pidslen = nprocs;
  return 0;
}

/* Create a process for TASK, which is not otherwise known to us.
   The PID/parentage/job-control fields are not yet filled in,
   and the proc is not entered into any hash table.  */
struct proc *
allocate_proc (task_t task)
{
  error_t err;
  struct proc *p;

  /* Pid 0 is us; pid 1 is init.  We handle those here specially;
     all other processes inherit from init here (though proc_child
     will move them to their actual parent usually).  */

  err = ports_create_port (proc_class, proc_bucket, sizeof (struct proc), &p);
  if (err)
    return NULL;

  memset (&p->p_pi + 1, 0, sizeof *p - sizeof p->p_pi);
  p->p_task = task;
  p->p_msgport = MACH_PORT_NULL;

  pthread_cond_init (&p->p_wakeup, NULL);

  return p;
}

/* Allocate and initialize the proc structure for init (PID 1),
   the original parent of all other procs.  */
struct proc *
create_startup_proc (void)
{
  static const uid_t zero;
  struct proc *p;
  const char *rootsname = "root";

  p = allocate_proc (MACH_PORT_NULL);
  assert (p);

  p->p_pid = HURD_PID_STARTUP;

  p->p_parent = p;
  p->p_sib = 0;
  p->p_prevsib = &p->p_ochild;
  p->p_ochild = p;
  p->p_parentset = 1;

  p->p_deadmsg = 1;		/* Force initial "re-"fetch of msgport.  */

  p->p_noowner = 0;
  p->p_id = make_ids (&zero, 1);
  assert (p->p_id);

  p->p_loginleader = 1;
  p->p_login = malloc (sizeof (struct login) + strlen (rootsname) + 1);
  assert (p->p_login);

  p->p_login->l_refcnt = 1;
  strcpy (p->p_login->l_name, rootsname);

  boot_setsid (p);

  return p;
}

/* Request a dead-name notification for P's task port.  */

void
proc_death_notify (struct proc *p)
{
  error_t err;
  mach_port_t old;

  err = mach_port_request_notification (mach_task_self (), p->p_task,
					MACH_NOTIFY_DEAD_NAME, 1,
					p->p_pi.port_right,
					MACH_MSG_TYPE_MAKE_SEND_ONCE,
					&old);
  assert_perror (err);

  if (old != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), old);
}

/* Complete a new process that has been allocated but not entirely initialized.
   This gets called for every process except startup_proc (PID 1).  */
void
complete_proc (struct proc *p, pid_t pid)
{
  /* Because these have a reference count of one before starting,
     they can never be freed, so we're safe. */
  static struct login *nulllogin;
  static struct ids nullids = { i_refcnt : 1, i_nuids : 0};
  const char nullsname [] = "<none>";

  if (!nulllogin)
    {
      nulllogin = malloc (sizeof (struct login) + sizeof (nullsname) + 1);
      nulllogin->l_refcnt = 1;
      strcpy (nulllogin->l_name, nullsname);
    }

  p->p_pid = pid;

  ids_ref (&nullids);
  p->p_id = &nullids;

  p->p_login = nulllogin;
  p->p_login->l_refcnt++;

  /* Our parent is init for now.  */
  p->p_parent = startup_proc;

  p->p_sib = startup_proc->p_ochild;
  p->p_prevsib = &startup_proc->p_ochild;
  if (p->p_sib)
    p->p_sib->p_prevsib = &p->p_sib;
  startup_proc->p_ochild = p;
  p->p_loginleader = 0;
  p->p_ochild = 0;
  p->p_parentset = 0;

  p->p_noowner = 1;

  p->p_pgrp = startup_proc->p_pgrp;

  proc_death_notify (p);
  add_proc_to_hash (p);
  join_pgrp (p);
}


/* Create a process for TASK, which is not otherwise known to us
   and initialize it in the usual ways.  */
static struct proc *
new_proc (task_t task)
{
  struct proc *p;

  p = allocate_proc (task);
  if (p)
    complete_proc (p, genpid ());
  return p;
}

/* The task associated with process P has died.  Drop most state,
   and then record us as dead.  Our parent will eventually complete the
   deallocation. */
void
process_has_exited (struct proc *p)
{
  /* We have already died; this can happen since both proc_reassign
     and dead-name notifications could result in two calls to this
     routine for the same process.  */
  if (p->p_dead)
    return;

  p->p_waited = 0;
  if (p->p_task != MACH_PORT_NULL)
    alert_parent (p);

  if (p->p_msgport)
    mach_port_deallocate (mach_task_self (), p->p_msgport);
  p->p_msgport = MACH_PORT_NULL;

  prociterate ((void (*) (struct proc *, void *))check_message_dying, p);

  /* Nuke external send rights and the (possible) associated reference.  */
  ports_destroy_right (p);

  if (!--p->p_login->l_refcnt)
    free (p->p_login);

  ids_rele (p->p_id);

  /* Reparent our children to init by attaching the head and tail
     of our list onto init's.  */
  if (p->p_ochild)
    {
      struct proc *tp;		/* will point to the last one.  */
      int isdead = 0;

      /* first tell them their parent is changing */
      for (tp = p->p_ochild; tp->p_sib; tp = tp->p_sib)
	{
	  if (tp->p_msgport != MACH_PORT_NULL)
	    nowait_msg_proc_newids (tp->p_msgport, tp->p_task,
				    1, tp->p_pgrp->pg_pgid,
				    !tp->p_pgrp->pg_orphcnt);
	  tp->p_parent = startup_proc;
	  if (tp->p_dead)
	    isdead = 1;
	}
      if (tp->p_msgport != MACH_PORT_NULL)
	nowait_msg_proc_newids (tp->p_msgport, tp->p_task,
				1, tp->p_pgrp->pg_pgid,
				!tp->p_pgrp->pg_orphcnt);
      tp->p_parent = startup_proc;

      /* And now append the lists. */
      tp->p_sib = startup_proc->p_ochild;
      if (tp->p_sib)
	tp->p_sib->p_prevsib = &tp->p_sib;
      startup_proc->p_ochild = p->p_ochild;
      p->p_ochild->p_prevsib = &startup_proc->p_ochild;

      if (isdead)
	alert_parent (startup_proc);
    }

  /* If an operation is in progress for this process, cause it
     to wakeup and return now. */
  if (p->p_waiting || p->p_msgportwait)
    pthread_cond_broadcast (&p->p_wakeup);

  p->p_dead = 1;

  /* Cancel any outstanding RPCs done on behalf of the dying process.  */
  ports_interrupt_rpcs (p);
}

void
complete_exit (struct proc *p)
{
  assert (p->p_dead);
  assert (p->p_waited);

  remove_proc_from_hash (p);
  if (p->p_task != MACH_PORT_NULL)
    mach_port_destroy (mach_task_self (), p->p_task);

  /* Remove us from our parent's list of children. */
  if (p->p_sib)
    p->p_sib->p_prevsib = p->p_prevsib;
  *p->p_prevsib = p->p_sib;

  leave_pgrp (p);

  /* Drop the reference we created long ago in new_proc.  The only
     other references that ever show up are those for RPC args, which
     will shortly vanish (because we are p_dead, those routines do
     nothing).  */
  ports_port_deref (p);
}

/* Get the list of all tasks from the kernel and start adding them.
   If we encounter TASK, then don't do any more and return its proc.
   If TASK is null or we never find it, then return 0. */
struct proc *
add_tasks (task_t task)
{
  mach_port_t *psets;
  size_t npsets;
  int i;
  struct proc *foundp = 0;

  host_processor_sets (mach_host_self (), &psets, &npsets);
  for (i = 0; i < npsets; i++)
    {
      mach_port_t psetpriv;
      mach_port_t *tasks;
      size_t ntasks;
      int j;

      if (!foundp)
	{
	  host_processor_set_priv (master_host_port, psets[i], &psetpriv);
	  processor_set_tasks (psetpriv, &tasks, &ntasks);
	  for (j = 0; j < ntasks; j++)
	    {
	      int set = 0;

	      /* The kernel can deliver us an array with null slots in the
		 middle, e.g. if a task died during the call.  */
	      if (! MACH_PORT_VALID (tasks[j]))
		continue;

	      if (!foundp)
		{
		  struct proc *p = task_find_nocreate (tasks[j]);
		  if (!p)
		    {
		      p = new_proc (tasks[j]);
		      set = 1;
		    }
		  if (!foundp && tasks[j] == task)
		    foundp = p;
		}
	      if (!set)
		mach_port_deallocate (mach_task_self (), tasks[j]);
	    }
	  munmap (tasks, ntasks * sizeof (task_t));
	  mach_port_deallocate (mach_task_self (), psetpriv);
	}
      mach_port_deallocate (mach_task_self (), psets[i]);
    }
  munmap (psets, npsets * sizeof (mach_port_t));
  return foundp;
}

/* Allocate a new unused PID.
   (Unused means it is neither the pid nor pgrp of any relevant data.) */
int
genpid ()
{
#define WRAP_AROUND 30000
#define START_OVER 100
  static int nextpid = 0;
  static int wrap = WRAP_AROUND;

  while (nextpid < wrap && !pidfree (nextpid))
    ++nextpid;
  if (nextpid >= wrap)
    {
      nextpid = START_OVER;
      while (!pidfree (nextpid))
	nextpid++;

      while (nextpid > wrap)
	wrap *= 2;
    }

  return nextpid++;
}
