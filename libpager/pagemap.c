/* Pagemap manipulation for pager library
   Copyright (C) 1994,97,99,2000,02 Free Software Foundation

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
#include <string.h>
#include <assert.h>

/* Start using vm_copy over memcpy when we have at least two
   pages.  */
#define VMCOPY_BETTER_THAN_MEMCPY (vm_page_size * 2)
  
/* Grow the pagemap of pager P as necessary to deal with page address
   OFF - 1.  */
error_t
_pager_pagemap_resize (struct pager *p, off_t off)
{
  error_t err = 0;

  assert (((p->pagemapsize * sizeof (*p->pagemap))
	   & (vm_page_size - 1)) == 0);

  if (p->pagemapsize <= off)
    {
      void *newaddr;
      int newsize = round_page (off * sizeof (*p->pagemap));

      newaddr = mmap (0, newsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (newaddr == MAP_FAILED)
	err = errno;
      else
	{
	  int oldsize = p->pagemapsize * sizeof (*p->pagemap);

	  if (oldsize > 0)
	    {
	      if (oldsize >= VMCOPY_BETTER_THAN_MEMCPY)
		{
		  err = vm_copy (mach_task_self (),
				 (vm_address_t) p->pagemap, oldsize,
				 (vm_address_t) newaddr);
		  assert_perror (err);
		}
	      else
		memcpy (newaddr, p->pagemap, oldsize);

	      munmap (p->pagemap, oldsize);
	    }

	  p->pagemap = newaddr;
	  p->pagemapsize = newsize / sizeof (*p->pagemap);
	}
    }

  return err;
}
