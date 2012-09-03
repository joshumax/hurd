/* Recording errors for pager library
   Copyright (C) 1994, 1997 Free Software Foundation

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


int _pager_page_errors[] = {KERN_SUCCESS, ENOSPC, EIO, EDQUOT};

/* Some error has happened indicating that the page cannot be written. 
   (Usually this is ENOSPC or EDQOUT.)  On the next pagein which
   requests write access, return the error to the kernel.  (This is 
   screwy because of the rules associated with m_o_lock_request.)
   Currently the only errors permitted are ENOSPC, EIO, and EDQUOT.  */
void
_pager_mark_next_request_error(struct pager *pager,
			       vm_address_t offset,
			       vm_size_t length,
			       error_t error)
{
  int page_error;
  short *p;

  offset /= __vm_page_size;
  length /= __vm_page_size;
  
  switch (error)
    {
    case 0:
      page_error = PAGE_NOERR;
      break;
    case ENOSPC:
      page_error = PAGE_ENOSPC;
      break;
    case EIO:
    default:
      page_error = PAGE_EIO;
      break;
    case EDQUOT:
      page_error = PAGE_EDQUOT;
      break;
    }
  
  for (p = pager->pagemap + offset; p < pager->pagemap + offset + length; p++)
    *p = SET_PM_NEXTERROR (*p, page_error);
}

/* We are returning a pager error to the kernel.  Write down
   in the pager what that error was so that the exception handling
   routines can find out.  (This is only necessary because the
   XP interface is not completely implemented in the kernel.)
   Again, only ENOSPC, EIO, and EDQUOT are permitted.  */
void
_pager_mark_object_error(struct pager *pager,
			 vm_address_t offset,
			 vm_size_t length,
			 error_t error)
{
  int page_error = 0;
  short *p;

  offset /= __vm_page_size;
  length /= __vm_page_size;
  
  switch (error)
    {
    case 0:
      page_error = PAGE_NOERR;
      break;
    case ENOSPC:
      page_error = PAGE_ENOSPC;
      break;
    case EIO:
    default:
      page_error = PAGE_EIO;
      break;
    case EDQUOT:
      page_error = PAGE_EDQUOT;
      break;
    }
  
  for (p = pager->pagemap + offset; p < pager->pagemap + offset + length; p++)
    *p = SET_PM_ERROR (*p, page_error);
}

/* Tell us what the error (set with mark_object_error) for 
   pager P is on page ADDR. */
error_t
pager_get_error (struct pager *p, vm_address_t addr)
{
  error_t err;
  
  pthread_mutex_lock (&p->interlock);

  addr /= vm_page_size;

  /* If there really is no error for ADDR, we should be able to exted the
     pagemap table; otherwise, if some previous operation failed because it
     couldn't extend the table, this attempt will *probably* (heh) fail for
     the same reason.  */
  err = _pager_pagemap_resize (p, addr);

  if (! err)
    err = _pager_page_errors[PM_ERROR(p->pagemap[addr])];

  pthread_mutex_unlock (&p->interlock);

  return err;
}
