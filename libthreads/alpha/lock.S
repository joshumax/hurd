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
 * $Log:	lock.s,v $
 * Revision 2.3  93/03/09  10:59:04  danner
 * 	Lost in previous merge:
 * 	   Added memory barriers where needed.
 * 	   Locks are longs now.
 * 	   [93/01/15            af]
 * 
 * Revision 2.2  93/01/14  18:04:31  danner
 * 	Mumble, "try_lock" really means "try_hard_once".
 * 	That is, try hard until you either get it or lose it.
 * 	[92/12/24            af]
 * 	Created a while back.
 * 	[92/12/10            af]
 * 
 */

#include <mach/alpha/asm.h>

/*
   The C interface for this function is

	boolean_t
	spin_try_lock_sw(m)
	long * m;

   The function has a slightly different semantics than TAS: it will
   return a boolean value that indicates whether the lock was acquired
   or not.  If not, we'll presume that the user will retry after some
   appropriate delay.
 */
	.text
	.align	4
	.set	noreorder

LEAF(spin_try_lock,1)
	mb
	ldq_l	t0,0(a0)
	or	zero,2,v0	/* build lock value */
	bne	t0,nope		/* already set, forget it */
	stq_c	v0,0(a0)	/* see if we still had the lock */
	beq	v0,yipe		/* if we just took an interrupt.. */
	RET			/* if v0 != 0 then we got it */
nope:
	mov	zero,v0		/* failed to acquire lock */
	RET
yipe:	br	zero,spin_try_lock	/* I love branch predictions.. */

	END(spin_try_lock)

LEAF(spin_unlock,1)
	mb			/* but this might be needed.. */
	stq	zero,0(a0)	/* no need for interlocks (sec 10.5.2) */
	mb			/* but this might be needed.. */
	RET

	END(spin_unlock)
