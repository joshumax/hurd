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
_pager_seqnos_memory_object_data_request (mach_port_t object,
					  mach_port_seqno_t seqno,
					  mach_port_t control,
					  vm_offset_t start_address,
					  vm_size_t length,
					  vm_prot_t access)
{
  error_t err;
  struct pager *p;
  short *pm_entries;
  off_t start;
  int npages;

  error_t last_error;
  int page_count;
  int good_pages;
  unsigned char *errors = NULL;

  int i;

  DEBUG ("object = %d, seqno = %d,control = %d, start_address = %d, "
	 "length = %d, access = %d",
	 object, seqno, control, start_address, length, access);

  p = ports_lookup_port (0, object, _pager_class);
  if (!p)
    return EOPNOTSUPP;

  /* Acquire the right to meddle with the pagemap */
  mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);

  /* sanity checks. */
  if (control != p->memobjcntl)
    {
      printf ("incg data request: wrong control port\n");
      goto release_out;
    }
  if (start_address % vm_page_size)
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

  start = start_address / vm_page_size;
  npages = length / vm_page_size;

  err = _pager_pagemap_resize (p, start + npages);
  if (err)
    goto allow_release_out;	/* Can't do much about the actual error.  */

  /* If someone is paging this out right now, the disk contents are
     unreliable, so we have to wait.  It is too expensive (right now)
     to find the data and return it, and then interrupt the write, so
     we just wait for the write to finish and then reread the
     requested pages from disk.  */

  pm_entries = &p->pagemap[start];

 retry:
  for (i = 0; i < npages; i ++)
    if (pm_entries[i] & PM_PAGINGOUT)
      {
	pm_entries[i] |= PM_WRITEWAIT;
	condition_wait (&p->wakeup, &p->interlock);
	goto retry;
      }

  last_error = 0;
  page_count = 0;
  good_pages = 0;

  for (i = 0; i < npages; i ++)
    {
      error_t err;

      pm_entries[i] |= PM_INCORE;

      if (pm_entries[i] & PM_INVALID)
	/* Data on disk was marked bad.  */
	err = PAGE_EIO;
      else if (PM_NEXTERROR (pm_entries[i]) != PAGE_NOERR
	       && (access & VM_PROT_WRITE))
	/* In the process of a request, flush, error.  */
	err = PM_NEXTERROR (pm_entries[i]);
      else
	{
	  good_pages ++;

	  if (last_error)
	    /* This is the start of a good range.  Flush the pending
	       error.  */
	    err = 0;
	  else
	    continue;
	}

      if (err == last_error)
	page_count ++;
      else
	{
	  if (!errors)
	    {
	      errors = alloca (sizeof (*errors) * npages);
	      memset (errors, 0, sizeof (*errors) * npages);
	    }

	  if (last_error)
	    {
	      off_t range_start = start + i - page_count;

	      /* Record the bad pages.  */
	      memset (errors + i - page_count, ~0,
		      sizeof (*errors) * page_count);

	      /* Tell the kernel about it.  */
	      memory_object_data_error (control, range_start * vm_page_size,
					page_count * vm_page_size,
					_pager_page_errors[last_error]);
	      _pager_mark_object_error (p, range_start, page_count,
					_pager_page_errors[last_error]);
	    }

	  last_error = err;
	  page_count = 1;
	}
    }

  if (last_error)
    {
      off_t range_start = start + npages - page_count;

      /* Record the bad pages.  */
      if (good_pages != 0)
	memset (errors + npages - page_count, ~0,
		sizeof (*errors) * page_count);

      memory_object_data_error (control, range_start * vm_page_size,
				page_count * vm_page_size,
				_pager_page_errors[last_error]);
      _pager_mark_object_error (p, range_start, page_count,
				_pager_page_errors[last_error]);
    }

  /* Let someone else in.  */
  _pager_release_seqno (p, seqno);
  mutex_unlock (&p->interlock);

  if (good_pages == npages)
    /* All of the pages are good.  Optimize the following loop
       away.  */
    p->ops->read (p, (struct user_pager_info *) &p->upi, start, npages);
  else if (good_pages > 0)
    {
      page_count = 1;
      last_error = errors[0];

      for (i = 1; i < npages; i ++)
	{
	  if (last_error == errors[i])
	    page_count ++;
	  else if (last_error)
	    /* Already reported the error, just reset the counters.  */
	    {
	      page_count = 1;
	      last_error = 0;
	    }
	  else
	    {
	      /* Read the pages and give them to the kernel.  */
	      p->ops->read (p, (struct user_pager_info *) &p->upi,
			    start + i - page_count, page_count);

	      last_error = errors[i];
	    }
	}

      if (last_error == 0)
	p->ops->read (p, (struct user_pager_info *) &p->upi,
		      start + i - page_count, page_count);
    }
  else
    /* No good data.  */
    ;

  mutex_lock (&p->interlock);
  _pager_allow_termination (p);
  mutex_unlock (&p->interlock);
  ports_port_deref (p);
  return 0;

 allow_release_out:
  _pager_allow_termination (p);
 release_out:
  _pager_release_seqno (p, seqno);
  mutex_unlock (&p->interlock);
  ports_port_deref (p);
  return 0;
}
