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
 * $Log: options.h,v $
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
 * Revision 2.8  91/05/14  17:58:35  mrt
 * 	Correcting copyright
 *
 * Revision 2.7  91/02/14  14:21:03  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:31  mrt]
 *
 * Revision 2.6  90/09/09  14:35:04  rpd
 * 	Remove special option , debug_mutex and thread_calls.
 * 	[90/08/24            rwd]
 *
 * Revision 2.5  90/06/02  15:14:14  rpd
 * 	Removed RCS Source, Header lines.
 * 	[90/05/03  00:07:27  rpd]
 *
 * Revision 2.4  90/03/14  21:12:15  rwd
 * 	Added new option:
 * 		WAIT_DEBUG:	keep track of who a blocked thread is
 * 				waiting for.
 * 	[90/03/01            rwd]
 *
 * Revision 2.3  90/01/19  14:37:25  rwd
 * 	New option:
 * 		THREAD_CALLS:	cthread_* version of thread_* calls.
 * 	[90/01/03            rwd]
 *
 * Revision 2.2  89/12/08  19:54:09  rwd
 * 	Added code:
 * 		MUTEX_SPECIAL:	Have extra kernel threads available for
 * 				special mutexes to avoid deadlocks
 * 	Removed options:
 * 		MSGOPT, RECEIVE_YIELD
 * 	[89/11/25            rwd]
 * 	Added option:
 * 		MUTEX_SPECIAL:	Allow special mutexes which will
 * 				garuntee the resulting threads runs
 * 				on a mutex_unlock
 * 	[89/11/21            rwd]
 * 	Options added are:
 * 		STATISTICS:	collect [kernel/c]thread state stats.
 * 		SPIN_RESCHED:	call swtch_pri(0) when spin will block.
 * 		MSGOPT:		try to minimize message sends
 * 		CHECK_STATUS:	check status of mach calls
 * 		RECEIVE_YIELD:	yield thread if no waiting threads after
 * 				cthread_msg_receive
 * 		RED_ZONE:	make redzone at end of stacks
 * 		DEBUG_MUTEX:	in conjunction with same in cthreads.h
 * 				use slow mutex with held=cproc_self().
 * 	[89/11/13            rwd]
 * 	Added copyright.  Removed all options.
 * 	[89/10/23            rwd]
 *
 */
/*
 * options.h
 */

/*#define STATISTICS*/
#define SPIN_RESCHED
/*#define CHECK_STATUS*/
/*#define RED_ZONE*/
/*#define WAIT_DEBUG*/
