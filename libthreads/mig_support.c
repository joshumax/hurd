/*
 * Mach Operating System
 * Copyright (c) 1993-1989 Carnegie Mellon University
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
 * HISTORY
 * 26-Feb-94  Johannes Helander (jvh) at Helsinki University of Technology
 *	Added mach_put_reply_port. mig_dealloc_reply_port now takes the
 *	port as an argument.
 *
 * $Log:	mig_support.c,v $
 * Revision 2.8  93/01/24  13:27:14  danner
 * 	Corrrected include of mach/mach.h to mach.h
 * 	[93/01/16            mrt]
 *
 * Revision 2.7  93/01/14  18:05:37  danner
 * 	Converted file to ANSI C.
 * 	Made argument to mig_init a void * for compatibility with
 * 	mig_init in libmach.
 * 	[92/12/18            pds]
 *
 * Revision 2.6  91/05/14  17:57:41  mrt
 * 	Correcting copyright
 *
 * Revision 2.5  91/02/14  14:20:30  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:26  mrt]
 *
 * Revision 2.4  90/08/07  14:31:41  rpd
 * 	Removed RCS keyword nonsense.
 *
 * Revision 2.3  90/08/07  14:27:48  rpd
 * 	When we recycle the global reply port by giving it to the first
 * 	cthread, clear the global reply port.  This will take care of
 * 	someone accidentally calling this twice.
 * 	[90/08/07            rwd]
 *
 * Revision 2.2  90/06/02  15:14:04  rpd
 * 	Converted to new IPC.
 * 	[90/03/20  20:56:50  rpd]
 *
 * Revision 2.1  89/08/03  17:09:50  rwd
 * Created.
 *
 * 18-Jan-89  David Golub (dbg) at Carnegie-Mellon University
 *	Replaced task_data() by thread_reply().
 *
 *
 * 27-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed mig_support.c to avoid deadlock that can occur
 *	if tracing is turned on	during calls to mig_get_reply_port().
 *
 * 10-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed mig_support.c to use MACH_CALL.
 *	Changed "is_init" to "multithreaded" and reversed its sense.
 *
 * 30-Jul-87  Mary Thompson (mrt) at Carnegie Mellon University
 *	Created.
 */
/*
 * 	File:	mig_support.c
 *	Author:	Mary R. Thompson, Carnegie Mellon University
 *	Date:	July, 1987
 *
 * 	Routines to set and deallocate the mig reply port for the current thread.
 * 	Called from mig-generated interfaces.
 *
 */


#include <mach.h>
#include <mach/mig_support.h>
#include <mach/mach_traps.h>
#include <cthreads.h>
#include "cthread_internals.h"

private boolean_t multithreaded = FALSE;
/* use a global reply port before becoming multi-threaded */
private mach_port_t mig_reply_port = MACH_PORT_NULL;

/*
 * Called by mach_init with 0 before cthread_init is
 * called and again with initial cproc at end of cthread_init.
 */
void
mig_init(register void *initial)
{
	if (initial == NO_CPROC) {
		/* called from mach_init before cthread_init,
		   possibly after a fork.  clear global reply port. */

		multithreaded = FALSE;
		mig_reply_port = MACH_PORT_NULL;
	} else {
		/* recycle global reply port as this cthread's reply port */

		multithreaded = TRUE;
		((cproc_t) initial)->reply_port = mig_reply_port;
		mig_reply_port = MACH_PORT_NULL;
	}
}

/*
 * Called by mig interface code whenever a reply port is needed.
 */
mach_port_t
mig_get_reply_port(void)
{
	register mach_port_t reply_port;

	if (multithreaded) {
		register cproc_t self;

		self = cproc_self();
		ASSERT(self != NO_CPROC);

		if ((reply_port = self->reply_port) == MACH_PORT_NULL)
			self->reply_port = reply_port = mach_reply_port();
	} else {
		if ((reply_port = mig_reply_port) == MACH_PORT_NULL)
			mig_reply_port = reply_port = mach_reply_port();
	}

	return reply_port;
}

/*
 * Called by mig interface code after a timeout on the reply port.
 * May also be called by user.
 */
/*ARGSUSED*/
void
mig_dealloc_reply_port(mach_port_t p)
{
	register mach_port_t reply_port;

	if (multithreaded) {
		register cproc_t self;

		self = cproc_self();
		ASSERT(self != NO_CPROC);

		reply_port = self->reply_port;
		self->reply_port = MACH_PORT_NULL;
	} else {
		reply_port = mig_reply_port;
		mig_reply_port = MACH_PORT_NULL;
	}

	(void) mach_port_mod_refs(mach_task_self(), reply_port,
				  MACH_PORT_RIGHT_RECEIVE, -1);
}

/*
 *  Called by mig interfaces when done with a port.
 *  Used to provide the same interface as needed when a custom
 *  allocator is used.
 */

/*ARGSUSED*/
void
mig_put_reply_port(mach_port_t port)
{
	/* Do nothing */
}

void
__mig_init(register void *initial)
{
  mig_init (initial);
}

mach_port_t
__mig_get_reply_port(void)
{
  return mig_get_reply_port();
}

void
__mig_dealloc_reply_port(mach_port_t p)
{
  mig_dealloc_reply_port(p);
}

void
__mig_put_reply_port(mach_port_t port)
{
  mig_put_reply_port(port);
}

void
__mig_dealloc_reply_port (void)
{
  mig_dealloc_reply_port ();
}

/* XXX shouldn't need these */
/* Called by MiG to allocate space.  */
void
__mig_allocate (vm_address_t *addr,
		vm_size_t size)
{
  if (__vm_allocate (__mach_task_self (), addr, size, 1) != KERN_SUCCESS)
    *addr = 0;
}

/* Called by MiG to deallocate space.  */
void
__mig_deallocate (vm_address_t addr,
		  vm_size_t size)
{
  (void) __vm_deallocate (__mach_task_self (), addr, size);
}
