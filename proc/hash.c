/* Hash table functions
   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation

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
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "proc.h"
#include <hurd/ihash.h>

static struct ihash pghash, pidhash, taskhash, sidhash;

/* Find the process corresponding to a given pid. */
struct proc *
pid_find (pid_t pid)
{
  struct proc *p;
  p = ihash_find (&pidhash, pid);
  return p->p_dead ? 0 : p;
}

/* Find the process corresponding to a given pid.  Return it even if
   it's dead. */
struct proc *
pid_find_allow_zombie (pid_t pid)
{
  return ihash_find (&pidhash, pid);
}

/* Find the process corresponding to a given task. */
struct proc *
task_find (task_t task)
{
  struct proc *p;
  p = ihash_find (&taskhash, task) ? : add_tasks (task);
  return p->p_dead ? 0 : p;
}

/* Find the process corresponding to a given task, but
   if we don't already know about it, just return 0. */
struct proc *
task_find_nocreate (task_t task)
{
  struct proc *p;
  p = ihash_find (&taskhash, task);
  return p->p_dead ? 0 : p;
}

/* Find the process corresponding to a given request port. */
struct proc *
reqport_find (mach_port_t reqport)
{
  struct proc *p;
  p = ports_lookup_port (proc_bucket, reqport, proc_class);
  if (p && p->p_dead)
    ports_port_deref (p);
  return p->p_dead ? 0 : p;
}

/* Find the process group corresponding to a given pgid. */
struct pgrp *
pgrp_find (pid_t pgid)
{
  return ihash_find (&pghash, pgid);
}

/* Find the session corresponding to a given sid. */
struct session *
session_find (pid_t sid)
{
  return ihash_find (&sidhash, sid);
}

/* Add a new process to the various hash tables. */
void
add_proc_to_hash (struct proc *p)
{
  ihash_add (&pidhash, p->p_pid, p, &p->p_pidhashloc);
  ihash_add (&taskhash, p->p_task, p, &p->p_taskhashloc);
}

/* Add a new process group to the various hash tables. */
void
add_pgrp_to_hash (struct pgrp *pg)
{
  ihash_add (&pghash, pg->pg_pgid, pg, &pg->pg_hashloc);
}

/* Add a new session to the various hash tables. */
void
add_session_to_hash (struct session *s)
{
  ihash_add (&sidhash, s->s_sid, s, &s->s_hashloc);
}

/* Remove a process group from the various hash tables. */
void
remove_pgrp_from_hash (struct pgrp *pg)
{
  ihash_locp_remove(0, pg->pg_hashloc);
}

/* Remove a process from the various hash tables. */
void
remove_proc_from_hash (struct proc *p)
{
  ihash_locp_remove(0, p->p_pidhashloc);
  ihash_locp_remove(0, p->p_taskhashloc);
}

/* Remove a session from the various hash tables. */
void
remove_session_from_hash (struct session *s)
{
  ihash_locp_remove(0, s->s_hashloc);
}

/* Call function FUN of two args for each process.  FUN's first arg is
   the process, its second arg is ARG. */
void
prociterate (void (*fun) (struct proc *, void *), void *arg)
{
  error_t thunk(void *value)
    {
      struct proc *p = value;
      if (!p->p_dead)
	(*fun)(p, arg);
      return 0;
    }
  ihash_iterate(&pidhash, thunk);
}

/* Tell if a pid is available for use */
int
pidfree (pid_t pid)
{
  return (!pid_find_allow_zombie (pid)
	  && !pgrp_find (pid) && !session_find (pid));
}
