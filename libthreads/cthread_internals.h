/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
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
 * $Log: cthread_internals.h,v $
 * Revision 1.3  1995/08/29 14:49:20  mib
 * (cproc_block): Provide decl.
 *
 * Revision 1.2  1994/05/05 10:58:01  roland
 * entered into RCS
 *
 * Revision 2.14  92/08/03  18:03:56  jfriedl
 * 	Made state element of struct cproc volatile.
 * 	[92/08/02            jfriedl]
 *
 * Revision 2.13  92/03/06  14:09:24  rpd
 * 	Added yield, defined using thread_switch.
 * 	[92/03/06            rpd]
 *
 * Revision 2.12  92/03/01  00:40:23  rpd
 * 	Removed exit declaration.  It conflicted with the real thing.
 * 	[92/02/29            rpd]
 *
 * Revision 2.11  91/08/28  11:19:23  jsb
 * 	Fixed MACH_CALL to allow multi-line expressions.
 * 	[91/08/23            rpd]
 *
 * Revision 2.10  91/07/31  18:33:33  dbg
 * 	Protect against redefinition of ASSERT.
 * 	[91/07/30  17:33:21  dbg]
 *
 * Revision 2.9  91/05/14  17:56:24  mrt
 * 	Correcting copyright
 *
 * Revision 2.8  91/02/14  14:19:42  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:02  mrt]
 *
 * Revision 2.7  90/11/05  14:36:55  rpd
 * 	Added spin_lock_t.
 * 	[90/10/31            rwd]
 *
 * Revision 2.6  90/09/09  14:34:51  rpd
 * 	Remove special field.
 * 	[90/08/24            rwd]
 *
 * Revision 2.5  90/06/02  15:13:44  rpd
 * 	Converted to new IPC.
 * 	[90/03/20  20:52:47  rpd]
 *
 * Revision 2.4  90/03/14  21:12:11  rwd
 * 	Added waiting_for field for debugging deadlocks.
 * 	[90/03/01            rwd]
 * 	Added list field to keep a master list of all cprocs.
 * 	[90/03/01            rwd]
 *
 * Revision 2.3  90/01/19  14:37:08  rwd
 * 	Keep track of real thread for use in thread_* substitutes.
 * 	Add CPROC_ARUN for about to run and CPROC_HOLD to avoid holding
 * 	spin_locks over system calls.
 * 	[90/01/03            rwd]
 * 	Add busy field to be used by cthread_msg calls to make sure we
 * 	have the right number of blocked kernel threads.
 * 	[89/12/21            rwd]
 *
 * Revision 2.2  89/12/08  19:53:28  rwd
 * 	Added CPROC_CONDWAIT state
 * 	[89/11/28            rwd]
 * 	Added on_special field.
 * 	[89/11/26            rwd]
 * 	Removed MSGOPT conditionals
 * 	[89/11/25            rwd]
 * 	Removed old debugging code.  Add wired port/flag.  Add state
 * 	for small state machine.
 * 	[89/10/30            rwd]
 * 	Added CPDEBUG code
 * 	[89/10/26            rwd]
 * 	Change TRACE to {x;} else.
 * 	[89/10/24            rwd]
 * 	Rewrote to work for limited number of kernel threads.  This is
 * 	basically a merge of coroutine and thread.  Added
 * 	cthread_receivce call for use by servers.
 * 	[89/10/23            rwd]
 *
 */
/*
 * cthread_internals.h
 *
 *
 * Private definitions for the C Threads implementation.
 *
 * The cproc structure is used for different implementations
 * of the basic schedulable units that execute cthreads.
 *
 */


#include "options.h"
#include <mach/port.h>
#include <mach/message.h>
#include <mach/thread_switch.h>

#if !defined(__STDC__) && !defined(volatile)
# ifdef __GNUC__
#   define volatile __volatile__
# else
#   define volatile /* you lose */
# endif
#endif

/*
 * Low-level thread implementation.
 * This structure must agree with struct ur_cthread in cthreads.h
 */
typedef struct cproc {
	struct cproc *next;		/* for lock, condition, and ready queues */
	cthread_t incarnation;		/* for cthread_self() */

	struct cproc *list;		/* for master cproc list */
#ifdef	WAIT_DEBUG
	volatile char *waiting_for;	/* address of mutex/cond waiting for */
#endif	 /* WAIT_DEBUG */

#if 0
	/* This is not needed in GNU; libc handles it.  */
	mach_port_t reply_port;		/* for mig_get_reply_port() */
#endif

	int context;
	spin_lock_t lock;
	volatile int state;			/* current state */
#define CPROC_RUNNING	0
#define CPROC_SWITCHING 1
#define CPROC_BLOCKED	2
#define CPROC_CONDWAIT	4

	mach_port_t wired;		/* is cthread wired to kernel thread */
	int busy;			/* used with cthread_msg calls */

	mach_msg_header_t msg;

	unsigned int stack_base;
	unsigned int stack_size;
} *cproc_t;

#define	NO_CPROC		((cproc_t) 0)
#define	cproc_self()		((cproc_t) ur_cthread_self())

int cproc_block ();

#if 0
/* This declaration conflicts with <stdlib.h> in GNU.  */
/*
 * C Threads imports:
 */
extern char *malloc();
#endif

/*
 * Mach imports:
 */
extern void mach_error();

/*
 * Macro for MACH kernel calls.
 */
#ifdef CHECK_STATUS
#define	MACH_CALL(expr, ret)	\
	if (((ret) = (expr)) != KERN_SUCCESS) { \
	quit(1, "error in %s at %d: %s\n", __FILE__, __LINE__, \
	     mach_error_string(ret)); \
	} else
#else  /* CHECK_STATUS */
#define MACH_CALL(expr, ret) (ret) = (expr)
#endif  /* CHECK_STATUS */

#define private static
#ifndef	ASSERT
#define ASSERT(x)
#endif
#define TRACE(x)

/*
 * What we do to yield the processor:
 * (This depresses the thread's priority for up to 10ms.)
 */

#define yield()		\
	(void) thread_switch(MACH_PORT_NULL, SWITCH_OPTION_DEPRESS, 10)
