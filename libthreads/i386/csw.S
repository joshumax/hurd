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
 * $Log: csw.S,v $
 * Revision 1.9  1998/07/20 06:58:28  roland
 * 1998-07-20  Roland McGrath  <roland@baalperazim.frob.com>
 *
 * 	* i386/csw.S (cproc_prepare): Take address of cthread_body as third
 * 	arg, so we don't have to deal with PIC magic to find its address
 * 	without producing a text reloc.
 * 	* cprocs.c (cproc_create): Pass &cthread_body to cproc_prepare.
 *
 * Revision 1.8  1997/04/04 01:31:16  thomas
 * Thu Apr  3 20:29:27 1997  Thomas Bushnell, n/BSG  <thomas@gnu.ai.mit.edu>
 *
 * 	* i386/csw.S: Define __ELF__ too.
 *
 * Revision 1.7  1996/10/24 19:30:10  thomas
 * Mon Oct 21 22:05:48 1996  Thomas Bushnell, n/BSG  <thomas@gnu.ai.mit.edu>
 *
 * 	* i386/csw.S (CALL_MCOUNT): New macro.
 * 	(cproc_swtich, cproc_start_wait, cproc_prepare): Use CALL_MCOUNT.
 *
 * Revision 1.6  1996/08/29 17:44:42  thomas
 * *** empty log message ***
 *
 * Revision 1.5  1995/10/04 20:55:28  roland
 * (JUMPTARGET): New macro, versions for [PIC] and not.
 * Use it in place of EXT.
 *
 * Revision 1.4  1995/10/04 20:22:17  roland
 * [PIC] (EXT): Redefine to use PLT.
 *
 * Revision 1.3  1995/05/12 18:35:55  roland
 * Use EXT macro instead of explicit underscores.
 *
# Revision 1.2  1994/05/04  19:01:50  mib
# entered into RCS
#
 * Revision 2.7  91/07/31  18:36:32  dbg
 * 	Fix for ANSI C preprocessor.
 * 	[91/07/30  17:35:16  dbg]
 *
 * Revision 2.6  91/05/14  17:56:56  mrt
 * 	Correcting copyright
 *
 * Revision 2.5  91/05/08  13:35:49  dbg
 * 	Unlock lock with a locked instruction (xchg).
 * 	[91/03/20            dbg]
 *
 * Revision 2.4  91/02/14  14:20:02  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/13  12:15:27  mrt]
 *
 * Revision 2.3  91/01/08  16:46:20  rpd
 * 	Don't use Times - horta doesn't like it for some reason.
 * 	[91/01/06            rpd]
 *
 * Revision 2.2  90/05/03  15:54:37  dbg
 * 	Created.
 * 	[90/02/05            dbg]
 *
 */
#define ELF
#undef	__ELF__
#define __ELF__ 1
#include <mach/i386/asm.h>

#ifdef	PIC
#define	JUMPTARGET(name)	EXT(name##@PLT)
#else
#define	JUMPTARGET(name)	EXT(name)
#endif

#ifdef PROF
#define CALL_MCOUNT \
  pushl %ebp; movl %esp, %ebp; call JUMPTARGET(mcount); popl %ebp;
#else
#define CALL_MCOUNT
#endif


/*
 * Suspend the current thread and resume the next one.
 *
 * void cproc_switch(int *cur, int *next, int *lock)
 */
ENTRY(cproc_switch)
	CALL_MCOUNT
	pushl	%ebp		/* save ebp */
	movl	%esp,%ebp	/* set frame pointer to get arguments */
	pushl	%ebx		/* save ebx */
	pushl	%esi		/*      esi */
	pushl	%edi		/*      edi */
	movl	B_ARG0,%eax	/* get cur */
	movl	%esp,(%eax)	/* save current esp */
	movl	B_ARG2,%edx	/* get address of lock before switching */
				/* stacks */
	movl	B_ARG1,%eax	/* get next */
	movl	(%eax),%esp	/* get new stack pointer */
	xorl	%eax,%eax	/* unlock */
	xchgl	%eax,(%edx)	/* the lock - now old thread can run */

	popl	%edi		/* restore di */
	popl	%esi		/*	  si */
	popl	%ebx		/*	  bx */
	popl	%ebp		/*     and bp (don`t use "leave" - bp */
				/*	      still points to old stack) */
	ret

/*
 * Create a new stack frame for a 'waiting' thread,
 * save current thread's frame, and switch to waiting thread.
 *
 * void cproc_start_wait(int *cur,
 *			 cproc_t child,
 *			 int stackp,
 *			 int *lock)
 */
ENTRY(cproc_start_wait)
	CALL_MCOUNT
	pushl	%ebp		/* save ebp */
	movl	%esp,%ebp	/* set frame pointer */
	pushl	%ebx		/* save ebx */
	pushl	%esi		/*      esi */
	pushl	%edi		/*      edi */
	movl	B_ARG0,%eax	/* get cur */
	movl	%esp,(%eax)	/* save current esp */
	movl	B_ARG1,%eax	/* get child thread */
	movl	B_ARG3,%edx	/* point to lock before switching stack */
	movl	B_ARG2,%esp	/* get new stack */
	pushl	%eax		/* push child thread as argument */
	movl	$0,%ebp		/* (clear frame pointer) */
	xorl	%eax,%eax	/* unlock */
	xchgl	%eax,(%edx)	/* the lock - now old thread can run */
	call	JUMPTARGET(cproc_waiting)/* call cproc_waiting */
	/*NOTREACHED*/

/*
 * Set up a thread's stack so that when cproc_switch switches to
 * it, it will start up as if it called
 * cproc_body(child)
 *
 * void cproc_prepare(cproc_t child, int *context, int stack,
 *		      void (*cthread_body)(cproc_t));
 */
ENTRY(cproc_prepare)
	CALL_MCOUNT
	pushl	%ebp		/* save ebp */
	movl	%esp,%ebp	/* set frame pointer */
	movl	B_ARG2,%edx	/* get child`s stack */
	subl	$28,%edx
				/* make room for context: */
				/* 0   saved edi () */
				/* 4   saved esi () */
				/* 8   saved ebx () */
				/* 12  saved ebp () */
				/* 16  return PC from cproc_switch */
				/* 20  return PC from cthread_body */
				/* 24  argument to cthread_body */
	movl	$0,12(%edx)	/* clear frame pointer */
	movl	B_ARG3,%ecx	/* get address of cthread_body passed in */
	movl	%ecx,16(%edx)	/* set child to resume at cthread_body */
	movl	$0,20(%edx)	/* fake return address from cthread_body */
	movl	B_ARG0,%ecx	/* get child thread pointer */
	movl	%ecx,24(%edx)	/* set as argument to cthread_body */
	movl	B_ARG1,%ecx	/* get pointer to context */
	movl	%edx,(%ecx)	/* save context */
	leave
	ret
