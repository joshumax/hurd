/* Memory wiring functions for default pager
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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


/* This file uses the "wrap" feature of GNU ld.  See the GNU ld
   documentation for more information on how this works.  
   The list of functions we wrap is specified in Makefile as
   $(subst-functions). */


error_t
__wrap___syscall_vm_allocate (task_t target_task,
			      vm_address_t *address,
			      vm_size_t size,
			      boolean_t anywhere)
{
  error_t err;
  
  err = __real___syscall_vm_allocate (target_task, address, size, anywhere);
  if (!err && target_task == mach_task_self ())
    wire_segment (*address, size);
  return err;
}

error_t
__wrap___vm_allocate_rpc (task_t target_task,
			  vm_address_t *address,
			  vm_size_t size,
			  boolean_t anywhere)
{
  error_t err;
  
  err = __real___vm_allocate_rpc (target_task, address, size, anywhere);
  if (!err && target_task == mach_task_self ())
    wire_segment (*address, size);
  return err;
}

error_t 
__wrap___syscall_vm_map (mach_port_t target_task,
			 vm_address_t *address,
			 vm_size_t size,
			 vm_address_t mask,
			 boolean_t anywhere,
			 mach_port_t memory_object,
			 vm_offset_t offset,
			 boolean_t copy,
			 vm_prot_t cur_protection,
			 vm_prot_t max_protection,
			 vm_inherit_t inheritance)
{
  error_t err;
  
  err = __real___syscall_vm_map (target_task, address, size, mask, anywhere,
				 memory_object, offset, copy, cur_protection,
				 max_protection, inheritance);
  if (!err && target_task == mach_task_self ())
    wire_segment (*address, size);
  return err;
}


error_t 
__wrap___vm_map_rpc (mach_port_t target_task,
		     vm_address_t *address,
		     vm_size_t size,
		     vm_address_t mask,
		     boolean_t anywhere,
		     mach_port_t memory_object,
		     vm_offset_t offset,
		     boolean_t copy,
		     vm_prot_t cur_protection,
		     vm_prot_t max_protection,
		     vm_inherit_t inheritance)
{
  error_t err;
  
  err = __real___vm_map_rpc (target_task, address, size, mask, anywhere,
			     memory_object, offset, copy, cur_protection,
			     mak_protection, inheritance);
  if (!err && target_task == mach_task_self ())
    wire_segment (*address, size);
  return err;
}

/* And the non-__ versions too. */

#define weak_alias(name,aliasname) \
  extern typeof (name) aliasname __attribute__ ((weak, alias (#name)));

weak_alias (__wrap___vm_map_rpc, __wrap_vm_map_rpc)
weak_alias (__wrap___syscall_vm_map, __wrap_syscall_vm_map)
weak_alias (__wrap___vm_allocate_rpc, __wrap_vm_allocate_rpc)
weak_alias (__wrap___syscall_vm_allocate, __wrap_syscall_vm_allocate)
