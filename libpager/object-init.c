/* Implementation of memory_object_init for pager library
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

#include "priv.h"
#include "memory_object.h"
#include <stdio.h>

/* Implement the object initialiation call as described in
   <mach/memory_object.defs>.  */
kern_return_t
_pager_seqnos_memory_object_init (mach_port_t object, 
				  mach_port_seqno_t seqno,
				  mach_port_t control,
				  mach_port_t name,
				  vm_size_t pagesize)
{
  struct pager *p;

  if (!(p = ports_check_port_type (object, pager_port_type)))
    return EOPNOTSUPP;

  if (pagesize != __vm_page_size)
    {
      printf ("incg init: bad page size");
      goto out;
    }

  mutex_lock (&p->interlock);

  _pager_wait_for_seqno (p, seqno);

  if (p->pager_state != NOTINIT)
    {
      printf ("pager dup init\n");
      goto out;
    }

  p->memobjcntl = control;
  p->memobjname = name;

  memory_object_ready (control, p->may_cache, p->copy_strategy);

  p->pager_state = NORMAL;

  _pager_release_seqno (p);
  mutex_unlock (&p->interlock);

 out:
  ports_done_with_port (p);
  return 0;
}
