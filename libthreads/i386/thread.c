/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
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
 * Revision 1.4  2001/02/26 04:15:27  roland
 * 2001-02-25  Roland McGrath  <roland@frob.com>
 *
 * 	* i386/thread.c: Remove superfluous bzero decl,
 * 	just include <strings.h> instead.
 *
 * Revision 1.3  1997/02/18 22:53:31  miles
 * (cproc_setup):
 *   Correctly leave space at top of stack for account for GNU per-thread
 *     variables.
 *
 * Revision 1.2  1994/05/04 19:05:26  mib
 * entered into RCS
 *
 * Revision 2.6  91/07/31  18:37:07  dbg
 * 	Undefine cthread_sp macro around function definition.
 * 	[91/07/30  17:36:23  dbg]
 *
 * Revision 2.5  91/05/14  17:57:27  mrt
 * 	Correcting copyright
 *
 * Revision 2.4  91/02/14  14:20:21  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/13  12:20:10  mrt]
 *
 * Revision 2.3  90/06/02  15:13:53  rpd
 * 	Added definition of cthread_sp.
 * 	[90/06/02            rpd]
 *
 * Revision 2.2  90/05/03  15:55:03  dbg
 * 	Created (from 68020 version).
 * 	[90/02/05            dbg]
 *
 */
/*
 * i386/thread.c
 *
 */

#ifndef	lint
static char rcs_id[] = "$Header: cvs-sans-libpthread/hurd/libthreads/i386/thread.c,v 1.5 2001/03/31 23:03:03 roland Exp $";
#endif	/* not lint */


#include "../cthreads.h"
#include "../cthread_internals.h"
#include <strings.h>
#include <mach/mach.h>


/*
 * Set up the initial state of a MACH thread
 * so that it will invoke cthread_body(child)
 * when it is resumed.
 */
void
cproc_setup(child, thread, routine)
	register cproc_t child;
	int thread;
	int routine;
{
	extern unsigned int __hurd_threadvar_max; /* GNU */
	register int *top = (int *)
	  cproc_stack_base (child,
			    sizeof(ur_cthread_t *) +
			    /* Account for GNU per-thread variables.  */
			    __hurd_threadvar_max *
			    sizeof (long int));
	struct i386_thread_state state;
	register struct i386_thread_state *ts = &state;
	kern_return_t r;
	unsigned int count;

	/*
	 * Set up i386 call frame and registers.
	 * Read registers first to get correct segment values.
	 */
	count = i386_THREAD_STATE_COUNT;
	MACH_CALL(thread_get_state(thread,i386_THREAD_STATE,(thread_state_t) &state,&count),r);

	ts->eip = routine;
	*--top = (int) child;	/* argument to function */
	*--top = 0;		/* fake return address */
	ts->uesp = (int) top;	/* set stack pointer */
	ts->ebp = 0;		/* clear frame pointer */

	MACH_CALL(thread_set_state(thread,i386_THREAD_STATE,(thread_state_t) &state,i386_THREAD_STATE_COUNT),r);
}

#ifdef	cthread_sp
#undef	cthread_sp
#endif

int
cthread_sp()
{
	int x;

	return (int) &x;
}
