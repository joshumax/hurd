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

error_t
mom_allocate_address (void *start, size_t len, int readonly,
		      struct mom_port_ref *obj, size_t offset,
		      int copy)
{
  error_t err;
  mach_port_t port;

  assert ((vm_address_t) start % vm_page_size == 0);
  assert (len % vm_page_size == 0);
  if (obj)
    {
      port = mom_fetch_mach_port (obj);
      assert (offset % vm_page_size == 0);
    }
  else
    port = MACH_PORT_NULL;
  
  mutex_lock (&_mom_memory_lock);
  err = vm_map (mach_task_self (), (vm_address_t *)&start, len, 0, 0,
		port, (obj ? offset : 0), (obj ? copy : 0),
		(VM_PROT_READ | VM_PROT_EXECUTE 
		 | (!readonly ? VM_PROT_WRITE : 0)),
		VM_PROT_READ | VM_PROT_EXECUTE | VM_PROT_WRITE,
		VM_INHERIT_COPY);
  mutex_unlock (&_mom_memory_lock);
  return err;
}
