/* Hash table functions
   Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation

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
#include <stddef.h>
#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>

#include "proc.h"
#include <hurd/ihash.h>

static struct hurd_ihash pghash
  = HURD_IHASH_INITIALIZER (offsetof (struct pgrp, pg_hashloc));
static struct hurd_ihash pidhash
  = HURD_IHASH_INITIALIZER (offsetof (struct proc, p_pidhashloc));
static struct hurd_ihash taskhash
  = HURD_IHASH_INITIALIZER (offsetof (struct proc, p_taskhashloc));
static struct hurd_ihash sidhash
  = HURD_IHASH_INITIALIZER (offsetof (struct session, s_hashloc));


/* Find the process corresponding to a given pid. */
struct proc *
pid_find (pid_t pid)
{
  struct proc *p;
  p = hurd_ihash_find (&pidhash, pid);
  return (!p || p->p_dead) ? 0 : p;
}

/* Find the process corresponding to a given pid.  Return it even if
   it's dead. */
struct proc *
pid_find_allow_zombie (pid_t pid)
{
  return hurd_ihash_find (&pidhash, pid);
}

/* Find the process corresponding to a given task. */
struct proc *
task_find (task_t task)
{
  struct proc *p;
  p = hurd_ihash_find (&taskhash, task) ? : add_tasks (task);
  return (!p || p->p_dead) ? 0 : p;
}

/* Find the process corresponding to a given task, but
   if we don't already know about it, just return 0. */
struct proc *
task_find_nocreate (task_t task)
{
  struct proc *p;
  p = hurd_ihash_find (&taskhash, task);
  return (!p || p->p_dead) ? 0 : p;
}

/* Find the process group corresponding to a given pgid. */
struct pgrp *
pgrp_find (pid_t pgid)
{
  return hurd_ihash_find (&pghash, pgid);
}

/* Find the session corresponding to a given sid. */
struct session *
session_find (pid_t sid)
{
  return hurd_ihash_find (&sidhash, sid);
}

/* Add a new process to the various hash tables. */
void
add_proc_to_hash (struct proc *p)
{
  hurd_ihash_add (&pidhash, p->p_pid, p);
  hurd_ihash_add (&taskhash, p->p_task, p);
}

/* Add a new process group to the various hash tables. */
void
add_pgrp_to_hash (struct pgrp *pg)
{
  hurd_ihash_add (&pghash, pg->pg_pgid, pg);
}

/* Add a new session to the various hash tables. */
void
add_session_to_hash (struct session *s)
{
  hurd_ihash_add (&sidhash, s->s_sid, s);
}

/* Remove a process group from the various hash tables. */
void
remove_pgrp_from_hash (struct pgrp *pg)
{
  hurd_ihash_locp_remove (&pghash, pg->pg_hashloc);
}

/* Remove a process from the various hash tables. */
void
remove_proc_from_hash (struct proc *p)
{
  hurd_ihash_locp_remove (&pidhash, p->p_pidhashloc);
  hurd_ihash_locp_remove (&taskhash, p->p_taskhashloc);
}

/* Remove a session from the various hash tables. */
void
remove_session_from_hash (struct session *s)
{
  hurd_ihash_locp_remove (&sidhash, s->s_hashloc);
}

/* Call function FUN of two args for each process.  FUN's first arg is
   the process, its second arg is ARG. */
void
prociterate (void (*fun) (struct proc *, void *), void *arg)
{
  HURD_IHASH_ITERATE (&pidhash, value)
    {
      struct proc *p = value;
      if (!p->p_dead)
	(*fun)(p, arg);
    }
}

/* Tell if a pid is available for use */
int
pidfree (pid_t pid)
{
  return (!pid_find_allow_zombie (pid)
	  && !pgrp_find (pid) && !session_find (pid));
}
