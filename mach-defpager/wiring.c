/*
 * Mach Operating System
 * Copyright (c) 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * Package to wire current task's memory.
 */
#include <mach.h>
#include <mach_init.h>
#include <mach/gnumach.h>
#include <mach/machine/vm_param.h>
#include "default_pager.h"

mach_port_t	this_task;		/* our task */
mach_port_t	priv_host_port = MACH_PORT_NULL;
					/* the privileged host port */

void
wire_setup(host_priv)
	mach_port_t	host_priv;
{
	priv_host_port = host_priv;
	this_task = mach_task_self();
}

void
wire_thread()
{
	kern_return_t	kr;

	if (priv_host_port == MACH_PORT_NULL)
	    return;

	kr = thread_wire(priv_host_port,
			 mach_thread_self(),
			 TRUE);
	if (kr != KERN_SUCCESS)
	    panic("wire_thread: %d", kr);
}

void
wire_all_memory()
{
	kern_return_t kr;
	vm_offset_t	address;
	vm_size_t	size;
	vm_prot_t	protection;
	vm_prot_t	max_protection;
	vm_inherit_t	inheritance;
	boolean_t	is_shared;
	memory_object_name_t object;
	vm_offset_t	offset;

	if (priv_host_port == MACH_PORT_NULL)
	    return;

	/* iterate through all regions, wiring */
	address = 0;
	while (
	    (kr = vm_region(this_task, &address,
	    		&size,
			&protection,
			&max_protection,
			&inheritance,
			&is_shared,
			&object,
			&offset))
		== KERN_SUCCESS)
	{
	    if (MACH_PORT_VALID(object))
		(void) mach_port_deallocate(this_task, object);
	    if (protection != VM_PROT_NONE)
	      {
		/* The VM system cannot cope with a COW fault on another
		   unrelated virtual copy happening later when we have
		   wired down the original page.  So we must touch all our
		   pages before wiring to make sure that only we will ever
		   use them.  */
		void *page;
		if (!(protection & VM_PROT_WRITE))
		  {
		    kr = vm_protect(this_task, address, size,
				    0, max_protection);
		  }
		for (page = (void *) address;
		     page < (void *) (address + size);
		     page += vm_page_size)
		  *(volatile int *) page = *(int *) page;

		kr = vm_wire(priv_host_port, this_task,
			     address, size, protection);
		if (kr != KERN_SUCCESS)
			panic("vm_wire: %d", kr);

		if (!(protection & VM_PROT_WRITE))
		  {
		    kr = vm_protect(this_task, address, size,
				    0, protection);
		  }
	      }
	    address += size;
	}

	/*
	 * Automatically wire down future mappings, including those
	 * that are currently PROT_NONE but become accessible.
	 */

	kr = vm_wire_all(priv_host_port, this_task, VM_WIRE_ALL);

	if (kr != KERN_SUCCESS) {
	    panic("wire_all_memory: %d", kr);
	}
}
