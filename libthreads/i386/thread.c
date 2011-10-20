/*
 * Mach Operating System
 * Copyright (c) 1992,1991,1990 Carnegie Mellon University
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
 * $Log:	thread.c,v $
 * Revision 2.8  93/02/02  21:54:58  mrt
 * 	Changed include of mach/mach.h to mach.h.
 * 	[93/02/02            mrt]
 *
 * Revision 2.7  93/01/14  18:05:15  danner
 * 	Converted file to ANSI C.
 * 	Fixed argument types.
 * 	[92/12/18            pds]
 *
 * Revision 2.6  91/07/31  18:37:07  dbg
 * 	Undefine cthread_sp macro around function definition.
 * 	[91/07/30  17:36:23  dbg]
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
char rcs_id[] = "$Header: cvs-sans-libpthread/hurd/libthreads/i386/thread.c,v 1.7 2002/05/27 02:50:10 roland Exp $";
#endif	/* not lint */


#include <cthreads.h>
#include "cthread_internals.h"
#include <mach.h>
#include <mach/i386/mach_i386.h>
#include <mach/mig_errors.h>

#define HURD_TLS_DESC_DECL(desc, tcb)					      \
  struct descriptor desc =						      \
    {				/* low word: */				      \
      0xffff			/* limit 0..15 */			      \
      | (((unsigned int) (tcb)) << 16) /* base 0..15 */			      \
      ,				/* high word: */			      \
      ((((unsigned int) (tcb)) >> 16) & 0xff) /* base 16..23 */		      \
      | ((0x12 | 0x60 | 0x80) << 8) /* access = ACC_DATA_W|ACC_PL_U|ACC_P */  \
      | (0xf << 16)		/* limit 16..19 */			      \
      | ((4 | 8) << 20)		/* granularity = SZ_32|SZ_G */		      \
      | (((unsigned int) (tcb)) & 0xff000000) /* base 24..31 */		      \
    }

/*
 * Set up the initial state of a MACH thread
 * so that it will invoke cthread_body(child)
 * when it is resumed.
 */
void
cproc_setup(register cproc_t child, thread_t thread, tcbhead_t *tcb, void (*routine)(cproc_t))
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
	HURD_TLS_DESC_DECL(desc, tcb);
	int sel;

	/*
	 * Set up i386 call frame and registers.
	 * Read registers first to get correct segment values.
	 */
	count = i386_THREAD_STATE_COUNT;
	MACH_CALL(thread_get_state(thread,i386_REGS_SEGS_STATE,(thread_state_t) &state,&count),r);

	ts->eip = (int) routine;
	*--top = (int) child;	/* argument to function */
	*--top = 0;		/* fake return address */
	ts->uesp = (int) top;	/* set stack pointer */
	ts->ebp = 0;		/* clear frame pointer */

	asm ("mov %%gs, %w0" : "=q" (sel) : "0" (0));
	tcb->tcb = tcb;
	tcb->self = thread;
	if (__builtin_expect (sel, 0x48) & 4) /* LDT selector */
	  __i386_set_ldt (thread, sel, &desc, 1);
	else
	  __i386_set_gdt (thread, &sel, desc);
	ts->gs = sel;

	MACH_CALL(thread_set_state(thread,i386_REGS_SEGS_STATE,(thread_state_t) &state,i386_THREAD_STATE_COUNT),r);
}

#if	defined(cthread_sp)
#undef	cthread_sp
#endif

int
cthread_sp(void)
{
	return (int) __thread_stack_pointer ();
}
