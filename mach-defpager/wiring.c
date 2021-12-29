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
