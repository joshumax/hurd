/* Implementation of memory_object_data_unlock for pager library
   Copyright (C) 1994,95,2002 Free Software Foundation, Inc.

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

/* Implement kernel requests for access as described in
   <mach/memory_object.defs>. */
kern_return_t
_pager_seqnos_memory_object_data_unlock (mach_port_t object,
					 mach_port_seqno_t seqno,
					 mach_port_t control,
					 vm_offset_t start,
					 vm_size_t length,
					 vm_prot_t access)
{
  struct pager *p;

  p = ports_lookup_port (0, object, _pager_class);
  if (!p)
    return EOPNOTSUPP;

  mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);
  _pager_release_seqno (p, seqno);
  mutex_unlock (&p->interlock);

  if (p->pager_state != NORMAL)
    {
      printf ("pager in wrong state for unlock\n");
      goto out;
    }

  if (control != p->memobjcntl)
    {
      printf ("incg data unlock: wrong control port\n");
      goto out;
    }
  /* The only thing we ever block is writes */
  if ((access & VM_PROT_WRITE) == 0)
    {
      printf ("incg data unlock: not unlock writes\n");
      goto out;
    }
  if (start % vm_page_size)
    {
      printf ("incg data unlock: misaligned request\n");
      goto out;
    }

  p->ops->unlock (p, (struct user_pager_info *) &p->upi,
		  start / vm_page_size, length / vm_page_size);

 out:
  ports_port_deref (p);
  return 0;
}
