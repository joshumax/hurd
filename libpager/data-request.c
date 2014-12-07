/* Implementation of memory_object_data_request for pager library
   Copyright (C) 1994,95,96,97,2000,02,10 Free Software Foundation

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
#include <string.h>

/* Implement pagein callback as described in <mach/memory_object.defs>. */
kern_return_t
_pager_S_memory_object_data_request (struct pager *p,
					  mach_port_t control,
					  vm_offset_t offset,
					  vm_size_t length,
					  vm_prot_t access)
{
  short *pm_entry;
  int doread, doerror;
  error_t err;
  vm_address_t page;
  int write_lock;

  if (!p
      || p->port.class != _pager_class)
    return EOPNOTSUPP;

  /* Acquire the right to meddle with the pagemap */
  pthread_mutex_lock (&p->interlock);

  /* sanity checks -- we don't do multi-page requests yet.  */
  if (control != p->memobjcntl)
    {
      printf ("incg data request: wrong control port\n");
      goto release_out;
    }
  if (length != __vm_page_size)
    {
      printf ("incg data request: bad length size %zd\n", length);
      goto release_out;
    }
  if (offset % __vm_page_size)
    {
      printf ("incg data request: misaligned request\n");
      goto release_out;
    }

  _pager_block_termination (p);	/* prevent termination until
				   mark_object_error is done */

  if (p->pager_state != NORMAL)
    {
      printf ("pager in wrong state for read\n");
      goto allow_release_out;
    }

  err = _pager_pagemap_resize (p, offset + length);
  if (err)
    goto allow_release_out;	/* Can't do much about the actual error.  */

  /* If someone is paging this out right now, the disk contents are
     unreliable, so we have to wait.  It is too expensive (right now) to
     find the data and return it, and then interrupt the write, so we just
     mark the page and have the writing thread do m_o_data_supply when it
     gets around to it.  */
  pm_entry = &p->pagemap[offset / __vm_page_size];
  if (*pm_entry & PM_PAGINGOUT)
    {
      doread = 0;
      *pm_entry |= PM_PAGEINWAIT;
    }
  else
    doread = 1;

  if (*pm_entry & PM_INVALID)
    doerror = 1;
  else
    doerror = 0;

  *pm_entry |= PM_INCORE;

  if (PM_NEXTERROR (*pm_entry) != PAGE_NOERR && (access & VM_PROT_WRITE))
    {
      memory_object_data_error (control, offset, length,
				_pager_page_errors[PM_NEXTERROR (*pm_entry)]);
      _pager_mark_object_error (p, offset, length,
				_pager_page_errors[PM_NEXTERROR (*pm_entry)]);
      *pm_entry = SET_PM_NEXTERROR (*pm_entry, PAGE_NOERR);
      doread = 0;
    }

  /* Let someone else in.  */
  pthread_mutex_unlock (&p->interlock);

  if (!doread)
    goto allow_term_out;
  if (doerror)
    goto error_read;

  err = pager_read_page (p->upi, offset, &page, &write_lock);
  if (err)
    goto error_read;

  memory_object_data_supply (p->memobjcntl, offset, page, length, 1,
			     write_lock ? VM_PROT_WRITE : VM_PROT_NONE,
			     p->notify_on_evict ? 1 : 0,
			     MACH_PORT_NULL);
  pthread_mutex_lock (&p->interlock);
  _pager_mark_object_error (p, offset, length, 0);
  _pager_allow_termination (p);
  pthread_mutex_unlock (&p->interlock);
  return 0;

 error_read:
  memory_object_data_error (p->memobjcntl, offset, length, EIO);
  _pager_mark_object_error (p, offset, length, EIO);
 allow_term_out:
  pthread_mutex_lock (&p->interlock);
  _pager_allow_termination (p);
  pthread_mutex_unlock (&p->interlock);
  return 0;

 allow_release_out:
  _pager_allow_termination (p);
 release_out:
  pthread_mutex_unlock (&p->interlock);
  return 0;
}
