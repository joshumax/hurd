/* 
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

#define EXC_TABLE_SIZE 1024	/* Ack!XXX -- should be dynamically sized!  */
/* This is the table of addresses for which we should report faults
   back to the faulting thread. */
static struct 
{
  struct pager *p;
  vm_offset_t pager_offset;
  void *offset;
  long length;
} memory_fault_table[EXC_TABLE_SIZE];

static spin_lock_t memory_fault_lock;

/* Mark the memory at ADDR continuing for LEN bytes as mapped from pager P
   at offset OFF.  Call when vm_map-ing part of the disk. 
   CAVEAT: addr must not be zero. */
void
register_memory_fault_area (struct pager *p,
			    vm_address_t off,
			    void *addr, 
			    long len)
{
  int i;

  assert (addr);

  spin_lock (&memory_fault_lock);
  
  for (i = 0; i < EXC_TABLE_SIZE; i++)
    if (!memory_fault_table[i].offset)
      {
	memory_fault_table[i].p = p;
	memory_fault_table[i].pager_offset = off;
	memory_fault_table[i].offset = addr;
	memory_fault_table[i].length = len;
	spin_unlock (&memory_fault_lock);
	return;
      }

  assert (0);
}

/* Mark the memory at ADDR continuing for LEN bytes as no longer
   mapped from the disk.  Call when vm_unmap-ing part of the disk.  */
void
unregister_memory_fault_area (void *addr,
			      long len)
{
  int i;

  assert (addr);

  spin_lock (&memory_fault_lock);
  for (i = 0; i < EXC_TABLE_SIZE; i++)
    if (memory_fault_table[i].offset == addr
	&& memory_fault_table[i].length == len)
      {
	memory_fault_table[i].offset = 0;
	spin_unlock (&memory_fault_lock);
	return;
      }
  assert (0);
}

/* Set up the exception handling system.  */
void
init_exceptions ()
{
  int i;

  for (i = 0; i < EXC_TABLE_SIZE; i++)
    memory_fault_table[i].offset = 0;
  spin_lock_init (&memory_fault_lock);

#if notdebugging
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &excport);
  mach_port_insert_right (mach_task_self (), excport, excport, 
			  MACH_MSG_TYPE_MAKE_SEND);
  task_get_special_port (mach_task_self (), TASK_EXCEPTION_PORT, &oldexcport);
  task_set_special_port (mach_task_self (), TASK_EXCEPTION_PORT, excport);
  mach_port_deallocate (mach_task_self (), excport);
#endif
}
