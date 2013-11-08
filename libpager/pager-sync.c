/* Functions for sync.
   Copyright (C) 1994, 1996 Free Software Foundation

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

/* Have the kernel write back all dirty pages in the pager; if
   WAIT is set, then wait for them to be finally written before
   returning. */
void
pager_sync (struct pager *p, int wait)
{
  vm_address_t offset;
  vm_size_t len;

  pager_report_extent (p->upi, &offset, &len);
  
  _pager_lock_object (p, offset, len, MEMORY_OBJECT_RETURN_ALL, 0,
		      VM_PROT_NO_CHANGE, wait);
}


/* Have the kernel write back some pages of a pager; if WAIT is set,
   then wait for them to be finally written before returning. */
void
pager_sync_some (struct pager *p, vm_address_t offset,
		 vm_size_t size, int wait)
{
  _pager_lock_object (p, offset, size, MEMORY_OBJECT_RETURN_ALL, 0,
		      VM_PROT_NO_CHANGE, wait);
}
