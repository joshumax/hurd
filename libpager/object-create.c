/* Create a new memory object (Default pager only)
   Copyright (C) 1994, 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */


#include "priv.h"
#include "memory_object_create_S.h"

/* Implement the object creation call as described in
   <mach/memory_object_default.defs>. */
kern_return_t
_pager_seqnos_memory_object_create (mach_port_t master,
				    mach_port_seqno_t seqno,
				    mach_port_t newobject,
				    vm_size_t objectsize,
				    mach_port_t newctl,
				    mach_port_t newname,
				    vm_size_t objectpagesize)
{
  struct port_info *masterpi;
  struct pager *p;

  if (!pager_support_defpager)
    return EOPNOTSUPP;
  
  if (objectpagesize != vm_page_size)
    return EINVAL;

  masterpi = ports_check_port_type (object, pager_master_port_type);
  if (!masterpi)
    return EOPNOTSUPP;
  
  p = ports_intern_external_port (newobject, sizeof (struct pager), 
				  pager_port_type);
  
  p->pager_state = NORMAL;
  mutex_init (&p->interlock);
  condition_init (&p->wakeup);
  p->lock_requests = 0;
  p->attribute_requests = 0;
  p->may_cache = 0;
  p->copy_strategy = MEMORY_OBJECT_COPY_DELAY;
  p->memobjcntl = newctl;
  p->memobjname = newname;
  p->seqno = -1;
  p->noterm = 0;
  p->waitingforseqno = 0;
  p->pagemap = 0;
  p->pagemapsize = 0;
  
  p->upi = pager_create_upi (p);

  ports_port_deref (p);
  return 0;
}

  
