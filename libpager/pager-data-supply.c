/* Supply data to the kernel.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Neal H Walfield

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
pager_data_supply (struct pager *pager,
		  int precious, int readonly,
		  off_t start, off_t npages,
		  void *buf, int dealloc)
{
  memory_object_data_supply (pager->memobjcntl, start * vm_page_size,
			    (vm_address_t) buf, npages * vm_page_size,
			    dealloc,
			    readonly ? VM_PROT_WRITE : VM_PROT_NONE,
			    precious, MACH_PORT_NULL);

  mutex_lock (&pager->interlock);
  _pager_pagemap_resize (pager, start + npages);
  _pager_mark_object_error (pager, start, npages, 0);
  mutex_unlock (&pager->interlock);

}
