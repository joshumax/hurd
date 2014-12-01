/*
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
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
 * $Log: thread.c,v $
 * Revision 1.1  2002/05/27 02:13:47  roland
 * 2002-05-26  Roland McGrath  <roland@frob.com>
 *
 * 	* alpha/cthreads.h, alpha/thread.c, alpha/csw.S, alpha/lock.S:
 * 	New files, verbatim from CMU release MK83a user/threads/alpha.
 *
 * Revision 2.3  93/02/01  09:56:49  danner
 * 	mach/mach.h -> mach.h
 * 	[93/01/28            danner]
 *
 * Revision 2.2  93/01/14  18:04:35  danner
 * 	Created.
 * 	[92/05/31            af]
 *
 */
/*
 * alpha/thread.c
 *
 * Cproc startup for ALPHA Cthreads implementation.
 */


#include <cthreads.h>
#include "cthread_internals.h"

#include <mach.h>

#if 0
/*
 * C library imports:
 */
extern bzero();
#endif

/*
 * Set up the initial state of a MACH thread
 * so that it will invoke routine(child)
 * when it is resumed.
 */
#warning TLS support not implemented
void
cproc_setup(
	register cproc_t child,
	thread_t	 thread,
	tcbhead_t	*tcb,
	void 	 	(*routine)(cproc_t))
{
	register integer_t			*top;
	struct alpha_thread_state		state;
	register struct alpha_thread_state	*ts;
	kern_return_t				r;

	/*
	 * Set up ALPHA call frame and registers.
	 */
	ts = &state;
	memset ((char *)ts, 0, sizeof(struct alpha_thread_state));

	top = (integer_t *) (child->stack_base + child->stack_size);

	/*
	 * Set pc & pv to procedure entry, pass one arg in register,
	 * allocate room for 6 regsave on the stack frame (sanity).
	 */
	ts->pc = (natural_t) routine;
	ts->r27 = (natural_t) routine;
	ts->r16 = (integer_t) child;
	ts->r30 = (integer_t) (top - 6);	/* see ARG_SAVE in csw.s */


	MACH_CALL(thread_set_state(thread,ALPHA_THREAD_STATE,(thread_state_t) &state,ALPHA_THREAD_STATE_COUNT),r);
}
