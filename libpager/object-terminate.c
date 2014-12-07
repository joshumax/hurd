/* Implementation of memory_object_terminate for pager library
   Copyright (C) 1994, 1995, 1996, 1999, 2000 Free Software Foundation

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

#include "priv.h"
#include "memory_object_S.h"
#include <stdio.h>

/* Implement the object termination call from the kernel as described
   in <mach/memory_object.defs>. */
kern_return_t
_pager_S_memory_object_terminate (struct pager *p,
				       mach_port_t control,
				       mach_port_t name)
{
  if (!p
      || p->port.class != _pager_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&p->interlock);

  if (control != p->memobjcntl)
    {
      printf ("incg terminate: wrong control port\n");
      goto out;
    }
  if (name != p->memobjname)
    {
      printf ("incg terminate: wrong name port\n");
      goto out;
    }

  while (p->noterm)
    {
      p->termwaiting = 1;
      pthread_cond_wait (&p->wakeup, &p->interlock);
    }

  /* Destry the ports we received; mark that in P so that it doesn't bother
     doing it again. */
  mach_port_destroy (mach_task_self (), control);
  mach_port_destroy (mach_task_self (), name);
  p->memobjcntl = p->memobjname = MACH_PORT_NULL;

  _pager_free_structure (p);

#ifdef KERNEL_INIT_RACE
  if (p->init_head)
    {
      struct pending_init *i = p->init_head;
      p->init_head = i->next;
      if (!i->next)
	p->init_tail = 0;
      p->memobjcntl = i->control;
      p->memobjname = i->name;
      memory_object_ready (i->control, p->may_cache, p->copy_strategy);
      p->pager_state = NORMAL;
      free (i);
    }
#endif

 out:
  pthread_mutex_unlock (&p->interlock);

  return 0;
}

/* Shared code for termination from memory_object_terminate and
   no-senders.  The pager must be locked.  This routine will
   deallocate all the ports and memory that pager P references.  */
void
_pager_free_structure (struct pager *p)
{
  int wakeup;
  struct lock_request *lr;
  struct attribute_request *ar;

  wakeup = 0;
  for (lr = p->lock_requests; lr; lr = lr->next)
    {
      lr->locks_pending = 0;
      if (!lr->pending_writes)
	wakeup = 1;
    }
  for (ar = p->attribute_requests; ar; ar = ar->next)
    {
      ar->attrs_pending = 0;
      wakeup = 1;
    }

  if (wakeup)
    pthread_cond_broadcast (&p->wakeup);

  if (p->memobjcntl != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), p->memobjcntl);
      p->memobjcntl = MACH_PORT_NULL;
    }
  if (p->memobjname != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), p->memobjname);
      p->memobjname = MACH_PORT_NULL;
    }

  /* Free the pagemap */
  if (p->pagemapsize)
    {
      munmap (p->pagemap, p->pagemapsize * sizeof (* p->pagemap));
      p->pagemapsize = 0;
      p->pagemap = 0;
    }
  
  p->pager_state = NOTINIT;
}
