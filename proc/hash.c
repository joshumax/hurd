/* Hash table functions
   Copyright (C) 1993, 1994 Free Software Foundation

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

#define HASH(pg) (id % ht->size)
#define REHASH(pg, h) ((id + (h * h)) % ht->size)

struct htable
{
  void **tab;
  int *ids;
  int size;
};
#define HASH_DEL ((void *) -1)

static struct htable pghash, pidhash, taskhash, porthash, sidhash, collhash;
static struct htable pgcollhash, sesscollhash;

/* Add ITEM to the hash table HT.  LOCP is the address of a pointer located
   in ITEM; *LOCP will be set to the address of ITEM's hash table pointer.
   The lookup key of ITEM is ID. */
static void
addhash (struct htable *ht,
	 void *item,
	 void ***locp,
	 int id)
{
  int h, firsth = -1;
  void **oldlist;
  int *oldids;
  int oldsize;
  int i;
  
  for (h = HASH (id); 
       (ht->tab[h] != 0 && ht->tab[h] != HASH_DEL && h != firsth);
       firsth = (firsth == -1) ? h : firsth, h = REHASH (id, firsth))
    ;

  if (ht->tab[h] == 0
      || ht->tab[h] == HASH_DEL)
    {
      ht->tab[h] = item;
      ht->ids[h] = id;
      *locp = &ht->tab[h];
      return;
    }
  
  /* We have to rehash this again? */
  oldlist = ht->tab;
  oldsize = ht->size;
  oldids = ht->ids;
  
  ht->size = nextprime (2 * ht->size);
  ht->tab = malloc (ht->size * sizeof (void *));
  ht->ids = malloc (ht->size * sizeof (int));
  bzero (ht->tab, (ht->size * sizeof (void *)));
  
  if (oldlist)
    for (i = 0; i < oldsize; i++)
      if (oldlist[i] != 0
	  && oldlist[i] != HASH_DEL)
	addhash (ht, oldlist[i], locp, oldids[i]);
  addhash (ht, item, locp, id);
  
  if (oldlist)
    {
      free (oldlist);
      free (oldids);
    }
}

/* Find and return the item in hash table HT with key ID. */
static void *
findhash (struct htable *ht,
	  int id)
{
  int h, firsth = -1;
  void *ret;

  for (h = HASH (id);
       (ht->tab[h] != 0	&& ht->ids[h] != id && h != firsth);
       firsth = (firsth == -1) ? h : firsth, h = REHASH (id, firsth))
    ;
  
  if (ht->ids[h] == id)
    ret =  ht->tab[h];
  else
    ret = 0;
  return ret;
}

/* Find the process corresponding to a given pid. */
struct proc *
pid_find (pid_t pid)
{
  return findhash (&pidhash, pid);
}

/* Find the process corresponding to a given task. */
struct proc *
task_find (task_t task)
{
  return findhash (&taskhash, task) ? : add_tasks (task);
}

/* Find the process corresponding to a given task, but
   if we don't already know about it, just return 0. */
struct proc *
task_find_nocreate (task_t task)
{
  return findhash (&taskhash, task);
}

/* Find the process corresponding to a given request port. */
struct proc *
reqport_find (mach_port_t reqport)
{
  return findhash (&porthash, reqport);
}

/* Find the process group corresponding to a given pgid. */
struct pgrp *
pgrp_find (pid_t pgid)
{
  return findhash (&pghash, pgid);
}

/* Find the session corresponding to a given sid. */
struct session *
session_find (pid_t sid)
{
  return findhash (&sidhash, sid);
}

/* Find the process collection corresponding to a gived refport. */
struct coll *
coll_find (mach_port_t refport)
{
  return findhash (&collhash, refport);
}

/* Add a new process to the various hash tables. */
void
add_proc_to_hash (struct proc *p)
{
  addhash (&pidhash, p, &p->p_pidhashloc, p->p_pid);
  addhash (&taskhash, p, &p->p_taskhashloc, p->p_task);
  addhash (&porthash, p, &p->p_porthashloc, p->p_reqport);
}

/* Add a new process group to the various hash tables. */
void
add_pgrp_to_hash (struct pgrp *pg)
{
  addhash (&pghash, pg, &pg->pg_hashloc, pg->pg_pgid);
  if (pg->pg_fakecoll)
    addhash (&pgcollhash, pg, &pg->pg_fakecollhashloc, pg->pg_fakecoll);
  else
    pg->pg_fakecollhashloc = 0;
}

/* A process group's fake collection port has changed; update
   the hash tables apporpriately. */
void
add_pgrp_fakecoll_to_hash (struct pgrp *pg)
{
  if (pg->pg_fakecollhashloc)
    *pg->pg_fakecollhashloc = HASH_DEL;
  addhash (&pgcollhash, pg, &pg->pg_fakecollhashloc, pg->pg_fakecoll);
}

/* Add a new process collection to the various hash tables. */
void
add_coll_to_hash (struct coll *c)
{
  addhash (&collhash, c, &c->c_hashloc, c->c_refport);
}

/* Add a new session to the various hash tables. */
void
add_session_to_hash (struct session *s)
{
  addhash (&sidhash, s, &s->s_hashloc, s->s_sid);
  if (s->s_fakecoll)
    addhash (&sesscollhash, s, &s->s_fakecollhashloc, s->s_fakecoll);
  else
    s->s_fakecollhashloc = 0;
}

/* A session's fake collection port has changes; update the hash tables
   appropriately. */
void
add_session_fakecoll_to_hash (struct session *s)
{
  if (s->s_fakecollhashloc)
    *s->s_fakecollhashloc = HASH_DEL;
  addhash (&sesscollhash, s, &s->s_fakecollhashloc, s->s_fakecoll);
}
				

/* Remove a process group from the various hash tables. */
void
remove_pgrp_from_hash (struct pgrp *pg)
{
  *pg->pg_hashloc = HASH_DEL;
  if (pg->pg_fakecollhashloc)
    *pg->pg_fakecollhashloc = HASH_DEL;
}

/* Remove a process from the various hash tables. */
void
remove_proc_from_hash (struct proc *p)
{
  *p->p_pidhashloc = HASH_DEL;
  *p->p_taskhashloc = HASH_DEL;
  *p->p_porthashloc = HASH_DEL;
}

/* Remove a process collection from the various hash tables. */
void
remove_coll_from_hash (struct coll *c)
{
  *c->c_hashloc = HASH_DEL;
}

/* Remove a session from the various hash tables. */
void
remove_session_from_hash (struct session *s)
{
  *s->s_hashloc = HASH_DEL;
  if (s->s_fakecollhashloc)
    *s->s_fakecollhashloc = HASH_DEL;
}

/* Call function FUN of two args for each process.  FUN's first arg is
   the process, its second arg is ARG. */
void
prociterate (void (*fun) (struct proc *, void *), void *arg)
{
  int i;
  
  for (i = 0; i < pidhash.size; i++)
    if (pidhash.tab[i] && pidhash.tab[i] != HASH_DEL)
      (*fun)(pidhash.tab[i], arg);
}

/* Call function FUN of two args for each process collection.  FUN's first
   arg is the collection, its second arg is ARG.  */
void
colliterate (void (*fun) (struct coll *, void *), void *arg)
{
  int i;

  for (i = 0; i < collhash.size; i++)
    if (collhash.tab[i] && collhash.tab[i] != HASH_DEL)
      (*fun)(collhash.tab[i], arg);
}

/* Tell if a pid is available for use */
int
pidfree (pid_t pid)
{
  return (!pid_find (pid) && !pgrp_find (pid)
	  && !session_find (pid) && !zombie_check_pid (pid));
}

