/* Pagemap manipulation for pager library
   Copyright (C) 1994, 1997, 1999, 2000 Free Software Foundation

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
  
/* Grow the pagemap of pager P as necessary to deal with address OFF */
error_t
_pager_pagemap_resize (struct pager *p, vm_address_t off)
{
  error_t err = 0;
  
  off /= __vm_page_size;

  if (p->pagemapsize < off)
    {
      void *newaddr;
      int newsize = round_page (off);

      newaddr = mmap (0, newsize * sizeof (*p->pagemap), 
		      PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      err = (newaddr == (void *) -1) ? errno : 0;
      if (! err)
	{
	  memcpy (newaddr, p->pagemap, p->pagemapsize * sizeof (*p->pagemap));
	  munmap (p->pagemap, p->pagemapsize * sizeof (*p->pagemap));
	  p->pagemap = newaddr;
	  p->pagemapsize = newsize;
	}
    }

  return err;
}
