/* Process information queries
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation

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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <assert.h>

#include "proc.h"
#include "process_S.h"

/* Implement S_proc_pid2task as described in <hurd/proc.defs>. */
kern_return_t
S_proc_pid2task (struct proc *callerp,
	       pid_t pid,
	       task_t *t)
{
  struct proc *p = pid_find (pid);
  
  if (!p)
    return ESRCH;
  
  if (!check_uid (callerp, p->p_owner))
    return EPERM;
  *t = p->p_task;

  return 0;
}

/* Implement proc_task2pid as described in <hurd/proc.defs>. */
kern_return_t
S_proc_task2pid (struct proc *callerp,
	       task_t t,
	       pid_t *pid)
{
  struct proc *p = task_find (t);
  
  if (!p)
    return ESRCH;
  
  *pid = p->p_pid;
  mach_port_deallocate (mach_task_self (), t);
  return 0;
}

/* Implement proc_task2proc as described in <hurd/proc.defs>. */
kern_return_t
S_proc_task2proc (struct proc *callerp,
		task_t t,
		mach_port_t *outproc)
{
  struct proc *p = task_find (t);
  
  if (!p)
    return ESRCH;
  
  *outproc = p->p_reqport;
  mach_port_deallocate (mach_task_self (), t);
  return 0;
}

/* Implement proc_proc2task as described in <hurd/proc.defs>. */
kern_return_t
S_proc_proc2task (struct proc *p,
		task_t *t)
{
  *t = p->p_task;
  return 0;
}

/* Implement proc_pid2proc as described in <hurd/proc.defs>. */
kern_return_t
S_proc_pid2proc (struct proc *callerp,
	       pid_t pid,
	       mach_port_t *outproc)
{
  struct proc *p = pid_find (pid);
  
  if (!p)
    return ESRCH;
  
  if (!check_uid (callerp, p->p_owner))
    return EPERM;
  
  *outproc = p->p_reqport;
  return 0;
}


/* Read a string starting at address ADDR in task T; set *STR to point at
   newly malloced storage holding it.  */
static error_t
get_string (task_t t,
	    vm_address_t addr,
	    char **str)
{
  /* This version assumes that a string is never more than one
     page in length.  */

  vm_address_t readaddr;
  vm_address_t data;
  u_int readlen;
  error_t err;
  char *c;

  readaddr = trunc_page (addr);
  err = vm_read (t, readaddr, vm_page_size * 2, &data, &readlen);
  if (err == KERN_INVALID_ADDRESS)
    err = vm_read (t, readaddr, vm_page_size, &data, &readlen);
  if (err == MACH_SEND_INVALID_DEST)
    err = ESRCH;
  if (err)
    return err;

  /* Scan for a null.  */
  c = memchr ((char *) (data + (addr - readaddr)), '\0',
	      readlen - (addr - readaddr));
  if (c == NULL)
    err = KERN_INVALID_ADDRESS;
  else
    {
      c++;			/* Include the null.  */
      *str = malloc (c - (char *)(data + (addr - readaddr)));
      if (*str == NULL)
	err = ENOMEM;
      else
	bcopy ((char *)(data + (addr - readaddr)), *str,
	       c - (char *)(data + (addr - readaddr)));
    }
  
  vm_deallocate (mach_task_self (), data, readlen);
  return err;
}

/* Read a vector of addresses (stored as are argv and envp) from tast TASK 
   found at address ADDR.  Set *VEC to point to newly malloced storage holding
   the addresses. */
static error_t
get_vector (task_t task,
	    vm_address_t addr,
	    int **vec)
{
  vm_address_t readaddr;
  vm_address_t data = 0;
  u_int readlen;
  error_t err;
  vm_address_t *t;

  readaddr = trunc_page (addr);
  err = vm_read (task, readaddr, vm_page_size * 2, &data, &readlen);
  if (err == KERN_INVALID_ADDRESS)
    err = vm_read (task, readaddr, vm_page_size, &data, &readlen);
  if (err == MACH_SEND_INVALID_DEST)
    err = ESRCH;
  if (err)
    return err;

  /* Scan for a null.  */
  *vec = 0;
  /* This will lose sometimes on machines with unfortunate alignment
     restrictions. XXX */
  for (t = (vm_address_t *) (data + (addr - readaddr)); 
       t < (vm_address_t *) (data + readlen);
       ++t)
    if (*t == 0)
      {
	++t;			/* Include the null.  */
	*vec = malloc ((char *)t - (char *)(data + (addr - readaddr)));
	if (*vec == NULL)
	  {
	    err = ENOMEM;
	    break;
	  }
	bcopy ((char *)(data + (addr - readaddr)), *vec,
	       (char *)t - (char *)(data + (addr - readaddr)));
	break;
      }

  if (!err && *vec == 0)
    err = KERN_INVALID_ADDRESS;
  
  vm_deallocate (mach_task_self (), data, readlen);
  return err;
}  

/* Fetch an array of strings at address LOC in task T into 
   BUF of size BUFLEN. */
static error_t 
get_string_array (task_t t,
		  vm_address_t loc,
		  vm_address_t *buf,
		  u_int *buflen)
{
  char *bp;
  int *vector, *vp;
  error_t err;
  vm_address_t origbuf = *buf;

  err = get_vector (t, loc, &vector);
  if (err)
    return err;
  
  bp = (char *) *buf;
  for (vp = vector; *vp; ++vp)
    {
      char *string;
      int len;

      err = get_string (t, *vp, &string);
      if (err)
	{
	  free (vector);
	  if (*buf != origbuf)
	    vm_deallocate (mach_task_self (), *buf, *buflen);
	  return err;
	}
      
      len = strlen (string) + 1;
      if (len > (char *) *buf + *buflen - bp)
	{
	  vm_address_t newbuf;
	  
	  err = vm_allocate (mach_task_self (), &newbuf, *buflen * 2, 1);
	  if (err)
	    {
	      free (string);
	      free (vector);
	      if (*buf != origbuf)
		vm_deallocate (mach_task_self (), *buf, *buflen);
	      return err;
	    }
	  bcopy (*(char **)buf, (void *)newbuf, (vm_address_t) bp - newbuf);
	  bp = newbuf + (bp - *buf);
	  if (*buf != origbuf)
	    vm_deallocate (mach_task_self (), *buf, *buflen);
	  *buf = newbuf;
	  *buflen *= 2;
	}
      bcopy (string, bp, len);
      bp += len;
      free (string);
    }
  free (vector);
  *buflen = bp - (char *) *buf;
  return 0;
}


/* Implement proc_getprocargs as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getprocargs (struct proc *callerp,
		  pid_t pid,
		  char **buf,
		  u_int *buflen)
{
  struct proc *p = pid_find (pid);
  
  if (!p)
    return ESRCH;
  
  return get_string_array (p->p_task, p->p_argv, (vm_address_t *) buf, buflen);
}

/* Implement proc_getprocenv as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getprocenv (struct proc *callerp,
		 pid_t pid,
		 char **buf,
		 u_int *buflen)
{
  struct proc *p = pid_find (pid);
  
  if (!p)
    return ESRCH;
  
  return get_string_array (p->p_task, p->p_envp, (vm_address_t *)buf, buflen);
}

/* Implement proc_getprocinfo as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getprocinfo (struct proc *callerp,
		    pid_t pid,
		    int flags,
		    int **piarray,
		    u_int *piarraylen,
		    char **noise, unsigned *noise_len)
{
  struct proc *p = pid_find (pid);
  struct procinfo *pi;
  int nthreads;
  thread_t *thds;
  error_t err = 0;
  size_t structsize;
  int i;
  int didalloc = 0;
  u_int tkcount, thcount;
  struct proc *tp;

  if (!p)
    return ESRCH;
  
 if (flags & (PI_FETCH_THREAD_SCHED | PI_FETCH_THREAD_BASIC
	      | PI_FETCH_THREAD_WAITS))
   flags |= PI_FETCH_THREADS;

  if (flags & PI_FETCH_THREADS)
    {
      err = task_threads (p->p_task, &thds, &nthreads);
      if (err == MACH_SEND_INVALID_DEST)
	err = ESRCH;
      if (err)
	return err;
    }
  else
    nthreads = 0;

  structsize =
    sizeof (struct procinfo) + nthreads * sizeof (pi->threadinfos[0]);

  if (structsize / sizeof (int) > *piarraylen)
    {
      vm_allocate (mach_task_self (), (u_int *)piarray, structsize, 1);
      didalloc = 1;
    }
  *piarraylen = structsize / sizeof (int);
  pi = (struct procinfo *) *piarray;
  
  pi->state = 
    ((p->p_stopped ? PI_STOPPED : 0)
     | (p->p_exec ? PI_EXECED : 0)
     | (p->p_waiting ? PI_WAITING : 0)
     | (!p->p_pgrp->pg_orphcnt ? PI_ORPHAN : 0)
     | (p->p_msgport == MACH_PORT_NULL ? PI_NOMSG : 0)
     | (p->p_pgrp->pg_session->s_sid == p->p_pid ? PI_SESSLD : 0)
     | (p->p_noowner ? PI_NOTOWNED : 0)
     | (!p->p_parentset ? PI_NOPARENT : 0)
     | (p->p_traced ? PI_TRACED : 0)
     | (p->p_msgportwait ? PI_GETMSG : 0)
     | (p->p_loginleader ? PI_LOGINLD : 0));
  pi->owner = p->p_owner;
  pi->ppid = p->p_parent->p_pid;
  pi->pgrp = p->p_pgrp->pg_pgid;
  pi->session = p->p_pgrp->pg_session->s_sid;
  for (tp = p; !tp->p_loginleader; tp = tp->p_parent)
    assert (tp);
  pi->logincollection = tp->p_pid;
  
  pi->nthreads = nthreads;
  
  if (flags & PI_FETCH_TASKINFO)
    {
      tkcount = TASK_BASIC_INFO_COUNT;
      err = task_info (p->p_task, TASK_BASIC_INFO, (int *)&pi->taskinfo,
		       &tkcount);
  
      if (err == MACH_SEND_INVALID_DEST)
	err = ESRCH;
    }
  
  for (i = 0; i < nthreads; i++)
    {
      pi->threadinfos[i].died = 0;
      if (flags & PI_FETCH_THREAD_BASIC)
	{
	  thcount = THREAD_BASIC_INFO_COUNT;
	  err = thread_info (thds[i], THREAD_BASIC_INFO,
			     (int *)&pi->threadinfos[i].pis_bi, 
			     &thcount);
	  if (err == MACH_SEND_INVALID_DEST)
	    {
	      pi->threadinfos[i].died = 1;
	      continue;
	    }
	  if (err && err != MACH_SEND_INVALID_DEST)
	    break;
	}

      if (flags & PI_FETCH_THREAD_SCHED)
	{
	  thcount = THREAD_SCHED_INFO_COUNT;
	  if (!err)
	    err = thread_info (thds[i], THREAD_SCHED_INFO,
			       (int *)&pi->threadinfos[i].pis_si,
			       &thcount);
	  if (err == MACH_SEND_INVALID_DEST)
	    {
	      pi->threadinfos[i].died = 1;
	      continue;
	    }
	  if (err && err != ESRCH)
	    break;
	}
      
      if (flags & PI_FETCH_THREAD_WAITS)
	/* Errors are not significant here. */
	msg_report_wait (p->p_msgport, thds[i], 
			 &pi->threadinfos[i].rpc_block);

      mach_port_deallocate (mach_task_self (), thds[i]);
    }
  
  if (flags & PI_FETCH_THREADS)
    {
      vm_deallocate (mach_task_self (), (u_int )thds,
		     nthreads * sizeof (thread_t));
    }
  if (err && didalloc)
    vm_deallocate (mach_task_self (), (u_int) *piarray, structsize);

  if (!err)
    /* Don't return anything for now.  */
    *noise_len = 0;

  return err;
}

/* Implement proc_make_login_coll as described in <hurd/process.defs>. */
kern_return_t
S_proc_make_login_coll (struct proc *p)
{
  p->p_loginleader = 1;
  return 0;
}

/* Implement proc_getloginid as described in <hurd/process.defs>. */
kern_return_t
S_proc_getloginid (struct proc *callerp,
		   pid_t pid,
		   pid_t *leader)
{
  struct proc *proc = pid_find (pid);
  struct proc *p;

  if (!proc)
    return ESRCH;

  for (p = proc; !p->p_loginleader; p = p->p_parent)
    assert (p);

  *leader = p->p_pid;
  return 0;
}

/* Implement proc_getloginpids as described in <hurd/process.defs>. */
kern_return_t
S_proc_getloginpids (struct proc *callerp,
		     pid_t id,
		     pid_t **pids,
		     u_int *npids)
{
  struct proc *l = pid_find (id);
  struct proc *p;
  struct proc **tail, **new, **parray;
  int parraysize;
  int i;
  
  if (!l || !l->p_loginleader)
    return ESRCH;
  
  /* Simple breadth first search of the children of L. */
  parraysize = 50;
  parray = malloc (sizeof (struct proc *) * parraysize);
  parray[0] = l;
  for (tail = parray, new = &parray[1]; tail != new; tail++)
    {
      for (p = (*tail)->p_ochild; p; p = p->p_sib)
	if (!p->p_loginleader)
	  {
	    /* Add P to the list at NEW */
	    if (new - parray > parraysize)
	      {
		struct proc **newparray;
		newparray = realloc (parray, ((parraysize *= 2) 
					      * sizeof (struct proc *)));
		tail = newparray + (tail - parray);
		new = newparray + (new - parray);
		parray = newparray;
	      }
	    *new++ = p;
	  }
    }

  if (*npids < new - parray)
    vm_allocate (mach_task_self (), (vm_address_t *) pids,
		 (new - parray) * sizeof (pid_t), 1);
  *npids = new - parray;
  for (i = 0; i < *npids; i++)
    (*pids)[i] = parray[i]->p_pid;
  free (parray);
  return 0;
}

/* Implement proc_setlogin as described in <hurd/proc.defs>. */
kern_return_t
S_proc_setlogin (struct proc *p,
	       char *login)
{
  struct login *l;

  if (!check_uid (p, 0))
    return EPERM;

  l = malloc (sizeof (struct login) + strlen (login) + 1);
  l->l_refcnt = 1;
  strcpy (l->l_name, login);
  if (!--p->p_login->l_refcnt)
    free (p->p_login);
  p->p_login = l;
  return 0;
}

/* Implement proc_getlogin as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getlogin (struct proc *p,
	       char *login)
{
  strcpy (login, p->p_login->l_name);
  return 0;
}

