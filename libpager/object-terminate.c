/* Implementation of memory_object_terminate for pager library
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

#include "priv.h"
#include "memory_object.h"
#include <stdio.h>

/* Implement the object termination call from the kernel as described
   in <mach/memory_object.defs>. */
kern_return_t
_pager_seqnos_memory_object_terminate (mach_port_t object, 
				       mach_port_seqno_t seqno,
				       mach_port_t control,
				       mach_port_t name)
{
  struct pager *p;
  
  if (!(p = check_port_type (object, pager_port_type)))
    return EOPNOTSUPP;
  
  if (control != p->memobjcntl)
    {
      printf ("incg terminate: wrong control port");
      goto out;
    }
  if (name != p->memobjname)
    {
      printf ("incg terminate: wrong name port");
      goto out;
    }

  mutex_lock (&p->interlock);

  _pager_wait_for_seqno (p, seqno);

  while (p->noterm)
    {
      p->termwaiting = 1;
      condition_wait (&p->wakeup, &p->interlock);
    }

  _pager_wait_for_seqno (p, seqno);

  while (p->noterm)
    {
      p->termwaiting = 1;
      condition_wait (&p->wakeup, &p->interlock);
    }

  _pager_free_structure (p);

 out:
  done_with_port (p);
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

  wakeup = 0;
  for (lr = p->lock_requests; lr; lr = lr->next)
    {
      lr->locks_pending = 0;
      if (!lr->pending_writes)
	wakeup = 1;
    }
  if (wakeup)
    condition_broadcast (&p->wakeup);

  mach_port_deallocate (mach_task_self (), p->memobjcntl);
  mach_port_deallocate (mach_task_self (), p->memobjname);

  /* Free the pagemap */
  if (p->pagemapsize)
    {
      vm_deallocate (mach_task_self (), (u_int)p->pagemap, p->pagemapsize);
      p->pagemapsize = 0;
      p->pagemap = 0;
    }
  
  p->pager_state = NOTINIT;
  _pager_release_seqno (p);

  mutex_unlock (&p->interlock);
}
