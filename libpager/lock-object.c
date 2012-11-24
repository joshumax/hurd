/* Synchronous wrapper for memory_object_lock_request
   Copyright (C) 1993, 1994, 1996, 1997, 2000 Free Software Foundation

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

/* Request a lock from the kernel on pager P.  Parameters OFFSET,
   SIZE, SHOULD_RETURN, SHOULD_FLUSH, and LOCK_VALUE are as for
   memory_object_lock_request.  If SYNC is set, then wait for the
   operation to fully complete before returning.  */
void
_pager_lock_object (struct pager *p, 
		    vm_offset_t offset,
		    vm_size_t size,
		    int should_return,
		    int should_flush,
		    vm_prot_t lock_value,
		    int sync)
{
  int i;
  struct lock_request *lr = 0;

  pthread_mutex_lock (&p->interlock);
  if (p->pager_state != NORMAL)
    {
      pthread_mutex_unlock (&p->interlock);
      return;
    }

  if (sync)
    {
      for (lr = p->lock_requests; lr; lr = lr->next)
	if (lr->start == offset && lr->end == offset + size)
	  {
	    lr->locks_pending++;
	    lr->threads_waiting++;
	    break;
	  }
      if (!lr)
	{
	  lr = malloc (sizeof (struct lock_request));
	  lr->start = offset;
	  lr->end = offset + size;
	  lr->pending_writes = 0;
	  lr->locks_pending = 1;
	  lr->threads_waiting = 1;
	  lr->next = p->lock_requests;
	  if (lr->next)
	    lr->next->prevp = &lr->next;
	  lr->prevp = &p->lock_requests;
	  p->lock_requests = lr;
	}
    }

  pthread_mutex_unlock (&p->interlock);
  memory_object_lock_request (p->memobjcntl, offset, size, should_return,
			      should_flush, lock_value, 
			      sync ? p->port.port_right : MACH_PORT_NULL);
  pthread_mutex_lock (&p->interlock);
  
  if (sync)
    {
      while (lr->locks_pending || lr->pending_writes)
	pthread_cond_wait (&p->wakeup, &p->interlock);
  
      if (! --lr->threads_waiting)
	{
	  *lr->prevp = lr->next;
	  if (lr->next)
	    lr->next->prevp = lr->prevp;
	  free (lr);
	}

      if (should_flush)
	{
	  vm_offset_t pm_offs = offset / __vm_page_size;

	  _pager_pagemap_resize (p, offset + size);
	  if (p->pagemapsize > pm_offs)
	    {
	      short *pm_entries = &p->pagemap[pm_offs];
	      vm_offset_t bound = size / vm_page_size;

	      if (bound > p->pagemapsize)
		bound = p->pagemapsize;

	      for (i = 0; i < bound; i++)
		pm_entries[i] &= ~PM_INCORE;
	    }
	}
    }

  pthread_mutex_unlock (&p->interlock);
}
