/* 
 * Mach Operating System
 * Copyright (c) 1993,1992 Carnegie Mellon University
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
 * $Log:	csw.s,v $
 * Revision 2.3  93/01/19  08:55:56  danner
 * 	Locks are longs now.  Put MBs before and after releasing
 * 	locks.
 * 	[93/01/15            af]
 * 
 * Revision 2.2  93/01/14  18:04:23  danner
 * 	Fixed bug in cproc_prepare, it was not setting up
 * 	the PV register properly.  Safer GP usage too.
 * 	ANSIfied comments.
 * 	[92/12/22  03:00:50  af]
 * 
 * 	Created.
 * 	[92/05/31            af]
 * 
 */
/*
 * alpha/csw.s
 *
 * Context switch and cproc startup for ALPHA COROUTINE implementation.
 */
#include <mach/alpha/asm.h>

	.text
	.align	4

#define	CSW_IMASK	\
	IM_S0|IM_S1|IM_S2|IM_S3|IM_S4|IM_S5|IM_S6|IM_RA|IM_GP

#define ARG_SAVE	(6*8)
#define SAVED_S0	(6*8)
#define SAVED_S1	(7*8)
#define SAVED_S2	(8*8)
#define SAVED_S3	(9*8)
#define SAVED_S4	(10*8)
#define SAVED_S5	(11*8)
#define SAVED_S6	(12*8)
#define SAVED_GP	(13*8)
#define SAVED_PC	(14*8)
#define SAVED_BYTES	(15*8)

/*
 * Suspend the current thread and resume the next one.
 *
 *	void
 *	cproc_switch(cur, next, lock)
 *		long *cur;
 *		long *next;
 *		simple_lock *lock;
 */
LEAF(cproc_switch,3)
	subq	sp,SAVED_BYTES,sp	/* allocate space for registers */
					/* Save them registers */
	stq	ra,SAVED_PC(sp)
	stq	gp,SAVED_GP(sp)
	stq	s0,SAVED_S0(sp)
	stq	s1,SAVED_S1(sp)
	stq	s2,SAVED_S2(sp)
	stq	s3,SAVED_S3(sp)
	stq	s4,SAVED_S4(sp)
	stq	s5,SAVED_S5(sp)
	stq	s6,SAVED_S6(sp)

	stq	sp,0(a0)		/* save current sp */
	ldq	sp,0(a1)		/* restore next sp */
					/* Reload them registers */
	ldq	ra,SAVED_PC(sp)
	ldq	gp,SAVED_GP(sp)
	ldq	s0,SAVED_S0(sp)
	ldq	s1,SAVED_S1(sp)
	ldq	s2,SAVED_S2(sp)
	ldq	s3,SAVED_S3(sp)
	ldq	s4,SAVED_S4(sp)
	ldq	s5,SAVED_S5(sp)
	ldq	s6,SAVED_S6(sp)
					/* return to next thread */
	.set	noreorder
	mb
	stq	zero,0(a2)		/* clear lock */
	mb
	.set	reorder
	addq	sp,SAVED_BYTES,sp
	RET
	END(cproc_switch)

/*
 *	void
 *	cproc_start_wait(parent_context, child, stackp, lock)
 *		long *parent_context;
 *		cproc_t child;
 *		long stackp;
 *		simple_lock *lock;
 */
NESTED(cproc_start_wait, 4, SAVED_BYTES, zero, CSW_IMASK, 0)
	ldgp	gp,0(pv)
	subq	sp,SAVED_BYTES,sp	/* allocate space for registers */
					/* Save parent registers */
	stq	ra,SAVED_PC(sp)
	stq	gp,SAVED_GP(sp)
	stq	s0,SAVED_S0(sp)
	stq	s1,SAVED_S1(sp)
	stq	s2,SAVED_S2(sp)
	stq	s3,SAVED_S3(sp)
	stq	s4,SAVED_S4(sp)
	stq	s5,SAVED_S5(sp)
	stq	s6,SAVED_S6(sp)

	stq	sp,0(a0)		/* save parent sp */

	.set	noreorder
	mb
	stq	zero,0(a3)		/* release lock */
	mb
	.set	reorder

	mov	a2,sp			/* get child sp */
	subq	sp,ARG_SAVE,sp		/* regsave (sanity) */
	mov	a1,a0
	CALL(cproc_waiting)		/* cproc_waiting(child) */
	/*
	 * Control never returns here.
	 */
	END(cproc_start_wait)

/*
 *	void
 *	cproc_prepare(child, child_context, stack)
 *		long	*child_context;
 *		long	*stack;
 */
LEAF(cproc_prepare,3)
	ldgp	gp,0(pv)
	subq	a2,ARG_SAVE,a2		/* cthread_body's fake frame */
	stq	a0,0(a2)		/* cthread_body(child) */
	subq	a2,SAVED_BYTES,a2	/* cproc_switch's ``frame'' */
	stq	s0,SAVED_S0(a2)
	stq	s1,SAVED_S1(a2)
	stq	s2,SAVED_S2(a2)
	stq	s3,SAVED_S3(a2)
	stq	s4,SAVED_S4(a2)
	stq	s5,SAVED_S5(a2)
	stq	s6,SAVED_S6(a2)
	stq	gp,SAVED_GP(a2)
	stq	a2,0(a1)		/* child context */
	lda	v0,1f
	stq	v0,SAVED_PC(a2)
	RET

	/*
	 *	The reason we are getting here is to load
	 *	arguments in registers where they are supposed
	 *	to be.  The code above only put the argument(s)
	 *	on the stack, now we'll load them.
	 */
1:	ldgp	gp,0(ra)		/* we get here from a cswitch */
	lda	v0,cthread_body
	ldq	a0,0(sp)
	mov	v0,ra
	mov	ra,pv			/* funcall or return, either way */
	RET
	END(cproc_prepare)

/*
 *	unsigned long
 *	cthread_sp()
 *
 *	Returns the current stack pointer.
 */

LEAF(cthread_sp,0)
	mov	sp, v0
	RET
	END(cthread_sp);
