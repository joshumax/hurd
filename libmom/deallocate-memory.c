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
mom_deallocate_memory (void *start, size_t len)
{
  error_t err;
  
  assert ((vm_address_t) start % vm_page_size == 0);
  assert (len % vm_page_size == 0);

  mutex_lock (&_mom_memory_lock);
  err = vm_deallocate (mach_task_self (), (vm_address_t) start, len);
  mutex_unlock (&_mom_memory_lock);
  return err;
}
