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
 * $Log:	cthreads.h,v $
 * Revision 2.4  93/11/17  19:00:42  dbg
 * 	When compiling with GCC, inline cthread_sp().
 * 	[93/09/21            af]
 * 
 * Revision 2.3  93/01/19  08:56:02  danner
 * 	Locks are now 64bits.
 * 	[92/12/30            af]
 * 
 * Revision 2.2  93/01/14  18:04:28  danner
 * 	Created.
 * 	[92/05/31            af]
 * 
 */

#ifndef _MACHINE_CTHREADS_H_
#define _MACHINE_CTHREADS_H_

typedef long spin_lock_t;
#define SPIN_LOCK_INITIALIZER 0
#define spin_lock_init(s) *(s)=0
#define spin_lock_locked(s) (*(s) != 0)

#if     defined(__GNUC__)

#define cthread_sp() \
        ({  register vm_offset_t _sp__; \
            __asm__("or $31,$30,%0" \
              : "=r" (_sp__) ); \
            _sp__; })

#endif  /* __GNUC__ */

#endif _MACHINE_CTHREADS_H_
