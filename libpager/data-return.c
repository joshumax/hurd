/* Implementation of memory_object_data_return for pager library
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
#include <string.h>

/* Called by the kernel to write data to the backing store */
kern_return_t
_pager_seqnos_memory_object_data_return (mach_port_t object, 
					 mach_port_seqno_t seqno,
					 mach_port_t control,
					 vm_offset_t offset,
					 pointer_t data,
					 vm_size_t length,
					 int dirty,
					 int kcopy)
{
  struct pager *p;
  char *pm_entry;
  error_t err;
  vm_size_t size, iosize;
  location_t loc;
  void *cookie;
  struct lock_request *lr;
  struct lock_list {struct lock_request *lr;
		    struct lock_list *next;} *lock_list, *ll;
  int write_lock;
  int wakeup;
  
  if (!(p = check_port_type (object, pager_port_type)))
    return EOPNOTSUPP;
  
  /* sanity checks -- we don't do multi-page requests yet.  */
  if (control != p->memobjcntl)
    {
      printf ("incg data request: wrong control port");
      err = 0;
      goto out;
    }
  if (length != __vm_page_size)
    {
      printf ("incg data request: bad length size");
      err = 0;
      goto out;
    }
  if (offset % __vm_page_size)
    {
      printf ("incg data request: misaligned request");
      err = 0;
      goto out;
    }

  if (!dirty)
    {
      err = 0;
      goto out;
    }

  /* Acquire the right to meddle with the pagemap */
  mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);

  if (p->pager_state != NORMAL)
    {
      printf ("pager in wrong state for write\n");
      _pager_release_seqno (p);
      mutex_unlock (&p->interlock);
      goto out;
    }

  _pager_block_termination (p);	/* until we are done with the pagemap
				   when the write completes. */

  _pager_pagemap_resize (p, offset + length);

  pm_entry = &p->pagemap[offset / __vm_page_size];

  if (*pm_entry & PM_PAGINGOUT)
    panic ("double pageout");
   
  /* Mark this page as being paged out.  */
  *pm_entry |= PM_PAGINGOUT;

  /* If this write occurs while a lock is pending, record
     it.  We have to keep this list because a lock request
     might come in while we do the I/O; in that case there
     would be a new entry on p->lock_requests and we must
     make sure we don't decrement it.  So we have to keep
     track independently of which lock requests we incremented. */
  lock_list = 0;
  for (lr = p->lock_requests; lr; lr = lr->next)
    if (offset < lr->end && offset + length >= lr->start)
      {
	ll = alloca (sizeof (struct lock_list));
	ll->lr = lr;
	ll->next = lock_list;
	lock_list = ll;
	lr->pending_writes++;
      }

  /* Let someone else in. */
  _pager_release_seqno (p);
  mutex_unlock (&p->interlock);

  err = pager_find_address (p->upi, offset, &loc, &cookie,
			    &size, &iosize, &write_lock);

  if (!err)
    {
      /* We throw away data in the page that extends beyond iosize; data
	 that is between size and iosize gets zeroed before being written. */
      if (size != iosize)
	bzero (data + size, iosize - size);
      err = pager_write_page (loc, cookie, data, iosize);
    }

  /* Acquire the right to meddle with the pagemap */
  mutex_lock (&p->interlock);
  _pager_pagemap_resize (p, offset + length);
  pm_entry = &p->pagemap[offset / __vm_page_size];

  if (err && ! (*pm_entry & PM_PAGEINWAIT))
    /* The only thing we can do here is mark the page, and give errors 
       from now on when it is to be read.  This is imperfect, because 
       if all users go away, the pagemap will be freed, and this information
       lost.  Oh well.  It's still better than Un*x.  Of course, if we 
       are about to hand this data to the kernel, the error isn't a problem,
       hence the check for pageinwait.  */
    *pm_entry |= PM_INVALID;

  if (*pm_entry & PM_PAGEINWAIT)
    memory_object_data_supply (p->memobjcntl, offset, data, length, 1,
			       VM_PROT_NONE, 0, MACH_PORT_NULL);
  else
    vm_deallocate (mach_task_self (), data, length);

  *pm_entry &= ~(PM_PAGINGOUT | PM_PAGEINWAIT);

  wakeup = 0;
  for (ll = lock_list; ll; ll = ll->next)
    if (!--ll->lr->pending_writes && !ll->lr->locks_pending)
      wakeup = 1;
  if (wakeup)
    condition_broadcast (&p->wakeup);

  _pager_allow_termination (p);

  mutex_unlock (&p->interlock);

  /* XXX can this really be done earlier inside pager_write_page? */
  /* Now it is OK for the file size to change, so we can release our lock.  */
  if (slp)
    {
      mutex_lock (slp);
      if (!--(*slip))
	condition_broadcast (slc);
      mutex_unlock (slp);
    }

 out:
  done_with_port (p);
  return 0;
}
