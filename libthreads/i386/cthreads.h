/* 
 * Mach Operating System
 * Copyright (c) 1993,1991,1990 Carnegie Mellon University
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
 * $Log: cthreads.h,v $
 * Revision 1.3  2007/03/03 23:57:37  sthibaul
 * 2006-03-04  Samuel Thibault  <samuel.thibault@ens-lyon.org>
 *
 * 	* libpthread/sysdeps/i386/machine-sp.h (thread_stack_pointer):
 * 	Optimize esp read.
 * 	* libpthread/i386/cthreads.h (cthread_sp): Likewise.
 *
 * Revision 1.2  2002/05/27 02:50:10  roland
 * 2002-05-26  Roland McGrath  <roland@frob.com>
 *
 * 	Changes merged from CMU MK83a version:
 * 	* cthreads.h, options.h: Various cleanups.
 * 	* call.c, cthread_data.c, sync.c, mig_support.c: Likewise.
 * 	* i386/cthreads.h, i386/thread.c, i386/lock.s: Likewise.
 * 	* cthread_internals.h: Add decls for internal functions.
 * 	(struct cproc): Use vm_offset_t for stack_base and stack_size members.
 * 	Use natural_t for context member.
 * 	* cprocs.c: Use prototypes for all defns.
 * 	* cthreads.c: Likewise.
 * 	(cthread_exit): Cast any_t to integer_t before int.
 *
 * Revision 2.9  93/01/24  13:24:58  danner
 * 	Move ! in spin_try_lock to give the compiler
 * 	a fighting chance.
 * 	[92/11/19            rvb]
 * 
 * Revision 2.8  93/01/14  18:05:09  danner
 * 	asm -> __asm__
 * 	[93/01/10            danner]
 * 
 * Revision 2.7  92/01/03  20:36:59  dbg
 * 	Add volatile to spin_lock_t.  Change spin_unlock and
 * 	spin_try_lock definitions back to memory operands, but rely on
 * 	volatile attribute to keep from using value in memory.
 * 	[91/09/04            dbg]
 * 
 * Revision 2.6  91/08/28  20:18:39  jsb
 * 	Safer definitions for spin_unlock and spin_try_lock from mib.
 * 
 * Revision 2.5  91/07/31  18:36:49  dbg
 * 	Add inline substitution for cthread_sp, spin_unlock,
 * 	spin_try_lock.
 * 	[91/07/30  17:35:53  dbg]
 * 
 * Revision 2.4  91/05/14  17:57:11  mrt
 * 	Correcting copyright
 * 
 * Revision 2.3  91/02/14  14:20:14  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/13  12:20:00  mrt]
 * 
 * Revision 2.2  90/11/05  14:37:23  rpd
 * 	Created.
 * 	[90/11/01            rwd]
 * 
 *
 */

#ifndef _MACHINE_CTHREADS_H_
#define _MACHINE_CTHREADS_H_

typedef volatile int spin_lock_t;
#define SPIN_LOCK_INITIALIZER	0
#define spin_lock_init(s)	(*(s) = 0)
#define spin_lock_locked(s)	(*(s) != 0)

#ifdef	__GNUC__

#define	spin_unlock(p) \
	({  register int _u__ ; \
	    __asm__ volatile("xorl %0, %0; \n\
			  xchgl %0, %1" \
			: "=&r" (_u__), "=m" (*(p)) :: "memory" ); \
	    0; })

#define	spin_try_lock(p)\
	(!({  boolean_t _r__; \
	    __asm__ volatile("movl $1, %0; \n\
			  xchgl %0, %1" \
			: "=&r" (_r__), "=m" (*(p)) :: "memory" ); \
	    _r__; }))

#define	cthread_sp() \
	({  register int	_sp__ asm("esp"); \
	    _sp__; })

#endif	/* __GNUC__ */

#endif _MACHINE_CTHREADS_H_
