/* Wrapper for unsolicited memory_object_data_supply
   Copyright (C) 1996, 1997, 2000 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */


#include "priv.h"

void
pager_offer_page (struct pager *p,
		  int precious,
		  int writelock,
		  vm_offset_t offset,
		  vm_address_t buf)
{
  pthread_mutex_lock (&p->interlock);

  if (_pager_pagemap_resize (p, offset + vm_page_size))
    {
      short *pm_entry = &p->pagemap[offset / vm_page_size];

      while (*pm_entry & PM_INCORE)
	{
	  pthread_mutex_unlock (&p->interlock);
	  pager_flush_some (p, offset, vm_page_size, 1);
	  pthread_mutex_lock (&p->interlock);
	}
      *pm_entry |= PM_INCORE;

      memory_object_data_supply (p->memobjcntl, offset, buf, vm_page_size, 0,
				 writelock ? VM_PROT_WRITE : VM_PROT_NONE, 
				 precious, MACH_PORT_NULL);
    }

  pthread_mutex_unlock (&p->interlock);
}
