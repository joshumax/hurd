/* Implementation of memory_object_data_request for pager library
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

/* Called by the kernel when data is needed upon page fault */
kern_return_t
seqnos_memory_object_data_request (mach_port_t object, 
				   mach_port_seqno_t seqno,
				   mach_port_t control,
				   vm_offset_t offset,
				   vm_size_t length,
				   vm_prot_t access)
{
  struct pager *p;
  char *pm_entry;
  int doread, doerror;
  error_t err;
  void *page;
  location_t loc;
  vm_size_t size, iosize;
  int write_lock;
  int extra_zeroes;
  vm_address_t min, max;

  if (!(p = check_port_type (object, pager_port_type)))
    return EOPNOTSUPP;
  
  /* sanity checks -- we don't do multi-page requests yet.  */
  if (control != p->memobjcntl)
    {
      printf ("incg data request: wrong control port");
      goto out;
    }
  if (length != __vm_page_size)
    {
      printf ("incg data request: bad length size");
      goto out;
    }
  if (offset % __vm_page_size)
    {
      printf ("incg data request: misaligned request");
      goto out;
    }

  /* Acquire the right to meddle with the pagemap */
  mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);

  _pager_block_termination (p);	/* prevent termination until 
				   mark_object_error is done */

  if (p->pager_state != NORMAL)
    {
      printf ("pager in wrong state for read\n");
      _pager_release_seqno (p);
      mutex_unlock (&p->interlock);
      goto allow_term_out;
    }

  _pager_pagemap_resize (p, offset + length);

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

  if (PM_NEXTERROR (*pm_entry) != PAGE_NOERR && (access & VM_PROT_WRITE))
    {
      memory_object_data_error (control, offset, length, 
				page_errors[PM_NEXTERROR (*pm_entry)]);
      _pager_mark_object_error (p, offset, length, 
				page_errors[PM_NEXTERROR (*pm_entry)]);
      *pm_entry = SET_PM_NEXTERROR (*pm_entry, PAGE_NOERR);
      doread = 0;
    }

  /* Let someone else in.  */
  _pager_release_seqno (p);
  mutex_unlock (&p->interlock);

  if (!doread)
    goto allow_term_out;
  if (doerror)
    goto error_read;

  err = pager_find_address (p->upi, offset, &loc, &size, &iosize, &write_lock);

  if (!err)
    error = pager_read_page (loc, &page, iosize, &extra_zeroes);

  if (err)
    goto error_read;
  
  if (size != __vm_page_size && !extra_zeroes)
    bzero (page + size, __vm_page_size - size);
  
  memory_object_data_supply (p->memobjcntl, offset, (u_int) page, length, 1,
			     write_lock ? VM_PROT_WRITE : VM_PROT_NONE, 0,
			     MACH_PORT_NULL);
  mutex_lock (&p->interlock);
  _pager_mark_object_error (p, offset, length, 0);
  _pager_allow_termination (p);
  mutex_unlock (&p->interlock);
  done_with_port (p);
  return 0;

 allow_term_out:
  mutex_lock (&p->interlock);
  _pager_allow_termination (p);
  mutex_unlock (&p->interlock);
 out:
  done_with_port (p);
  return 0;
  
 error_read:
  memory_object_data_error (p->memobjcntl, offset, length, EIO);
  _pager_mark_object_error (p, offset, length, EIO);
  _pager_allow_termination (p);
  done_with_port (p);
  return 0;
}
