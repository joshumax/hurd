/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
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

/* Have all dirty pages written back, and also flush the contents of
   the kernel's cache. */
void
pager_return (struct pager *p, int wait)
{
  vm_address_t offset;
  vm_size_t len;
  
  pager_report_extent (p->upi, &offset, &len);
  
  _pager_lock_object (p, offset, len, MEMORY_OBJECT_RETURN_ALL, 1,
		      VM_PROT_NO_CHANGE, wait);
}

void
pager_return_some (struct pager *p, vm_address_t offset,
		   vm_size_t size, int wait)
{
  _pager_lock_object (p, offset, size, MEMORY_OBJECT_RETURN_ALL, 1,
		      VM_PROT_NO_CHANGE, wait);
}
