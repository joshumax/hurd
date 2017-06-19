/*
 * Mach Operating System
 * Copyright (c) 1993-1989 Carnegie Mellon University
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
 * 26-Oct-94 Johannes Helander (jvh) Helsinki University of Technology
 *	Set the wait_type field.
 *
 * $Log: cprocs.c,v $
 * Revision 1.16  2002/05/27 02:50:10  roland
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
 * Revision 2.18  93/03/09  10:59:10  danner
 * 	Lint.
 * 	[93/03/06            af]
 *
 * Revision 2.17  93/01/19  08:55:44  danner
 * 	Added missing spin_lock_t type from cproc_list_lock decl.
 * 	[92/12/30            af]
 *
 *
 * Revision 2.16  93/01/14  18:04:46  danner
 * 	Convert file to ANSI C.
 * 	[92/12/18            pds]
 * 	64bit cleanup.
 * 	[92/12/10  21:08:32  af]
 *
 * Revision 2.15  92/03/06  14:09:31  rpd
 * 	Replaced swtch_pri with yield.
 * 	[92/03/06            rpd]
 *
 * Revision 2.14  91/08/28  11:19:16  jsb
 * 	Fixed the loop in cproc_fork_child that frees cprocs.
 * 	[91/08/23            rpd]
 *
 * Revision 2.13  91/07/31  18:33:04  dbg
 * 	Fix some more bad types.  Ints are NOT pointers.
 *
 * 	Fix argument type mismatch in cproc_create.
 * 	[91/07/30  17:32:59  dbg]
 *
 * Revision 2.12  91/05/14  17:56:11  mrt
 * 	Correcting copyright
 *
 * Revision 2.11  91/02/14  14:19:26  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:40:50  mrt]
 *
 * Revision 2.10  90/11/05  14:36:41  rpd
 * 	Added cproc_fork_{prepare,parent,child}.
 * 	[90/11/02            rwd]
 *
 * 	Fix for positive stack growth.
 * 	[90/11/01            rwd]
 *
 * 	Add spin_lock_t.
 * 	[90/10/31            rwd]
 *
 * Revision 2.9  90/10/12  13:07:12  rpd
 * 	Fix type
 * 	[90/10/10  15:09:59  rwd]
 *
 * 	Comment code.
 * 	[90/10/02            rwd]
 *
 * Revision 2.8  90/09/09  14:34:44  rpd
 * 	Remove special mutex.  Remove thread_calls and debug_mutex
 * 	[90/08/24            rwd]
 * 	Fix up old call to cthread_msg_busy to new format.
 * 	[90/08/22            rwd]
 *
 * Revision 2.7  90/08/06  15:09:17  rwd
 * 	Fixed arguments to cthread_mach_msg.
 * 	[90/06/26            rwd]
 * 	Add additional STATISTICS.
 * 	[90/06/07            rwd]
 *
 * 	Attempt to reduce number of times a cthread is released to to a
 * 	msg_receive by adding min/max instead of single number to
 * 	cthread_msg calls.
 * 	[90/06/06            rwd]
 *
 * Revision 2.6  90/06/02  15:13:36  rpd
 * 	Converted to new IPC.
 * 	[90/03/20  20:46:16  rpd]
 *
 * Revision 2.5  90/05/29  18:40:11  rwd
 * 	Don't incr special field until the mutex grab is successful.
 * 	[90/05/09            rwd]
 *
 * Revision 2.4  90/03/14  21:12:02  rwd
 * 	Added WAIT_DEBUG code for deadlock debugging.
 * 	[90/03/01            rwd]
 * 	Insert cprocs in cproc_list as allocated.
 * 	[90/03/01  10:20:16  rwd]
 *
 * Revision 2.3  90/01/19  14:36:57  rwd
 * 	Make cthread_msg_busy only release new thread if this is still
 * 	busy.  Ie don't release two on back to back calls.
 * 	[90/01/11            rwd]
 * 	Add THREAD_CALL code.  Add CPROC_ARUN state.
 * 	[90/01/03            rwd]
 * 	Add new cthread_msg_rpc call
 * 	[89/12/20            rwd]
 * 	Change cproc_self pointer to top of stack.  Now need to change
 * 	the stack of the first thread.
 * 	[89/12/12            rwd]
 *
 * Revision 2.2  89/12/08  19:53:13  rwd
 * 	Added CPROC_CONDWAIT state to deal with lock held
 * 	across mutex_unlock problem.
 * 	[89/11/29            rwd]
 * 	Changed mutexes to not hand off.  MUTEX_EXTRA conditional is
 * 	now obsolete.
 * 	[89/11/27            rwd]
 *
 * 	Add MUTEX_EXTRA code for extra kernel threads to serve special
 * 	mutexes in time of need.
 * 	[89/11/25            rwd]
 * 	Add MUTEX_SPECIAL and DEBUG_MUTEX code
 * 	[89/11/24            rwd]
 * 	Changed mutex_lock to mutex_lock_solid.  Mutex_lock is now a
 * 	macro which tries the spin_lock before making a subroutine call.
 * 	Mutex_unlock is now a macro with mutex_unlock_solid for worst case.
 * 	[89/11/13            rwd]
 *
 * 	Rewrite most to merge coroutine and thread implementation.
 * 	New routines are cthread_set_kernel_limit, cthread_kernel_limit,
 * 	cthread_wire, cthread_unwire, and cthread_receive.
 * 	[89/10/23            rwd]
 *
 * Revision 2.1  89/08/03  17:07:10  rwd
 * Created.
 *
 * 11-Apr-89  David Golub (dbg) at Carnegie-Mellon University
 *	Made condition_yield loop break if swtch_pri returns TRUE (in
 *	case we fix it).
 *
 * 31-Mar-89  David Golub (dbg) at Carnegie-Mellon University
 *	Change cond_signal, cond_broadcast, and cproc_continue so that
 *	the condition's spin lock is not held while continuing the
 *	process.
 *
 * 16-Jan-89  David Golub (dbg) at Carnegie-Mellon University
 *	Changes for stand-alone library to run on pure kernel:
 *	. made IPC_WAIT standard, as calls that are used if IPC_WAIT == 0
 *	  vanished a year ago.
 *	. Removed (as much as possible) references to stdio or other U*X
 *	  features.
 *
 *
 * 01-Apr-88  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed condition_clear(c) to acquire c->lock,
 *	to serialize after any threads still doing condition_signal(c).
 *	Suggested by Dan Julin.
 *
 * 19-Feb-88  Eric Cooper (ecc) at Carnegie Mellon University
 * 	Extended the inline scripts to handle spin_unlock() and mutex_unlock().
 *
 * 28-Jan-88  David Golub (dbg) at Carnegie Mellon University
 *	Removed thread_data argument from thread_create
 *	and converted to new thread_set_state call.
 *
 * 01-Dec-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Added inline expansion for cthread_sp() function.
 *
 * 21-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Fixed uninitialized reply_port in cproc_alloc() (found by rds).
 *
 * 14-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Tried using return value of swtch() to guide condition_wait().
 *	Performance was worse than using a hybrid spin/yield/block
 *	scheme, so the version using swtch() was commented out.
 *	Disabled IPC_WAIT in released version.
 *
 * 13-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Added IPC_WAIT option.
 *	If defined, thread synchronization (condition_wait() and
 *	cproc_continue()) are implemented using	msg_receive() and
 *	msg_send() instead of thread_suspend() and thread_resume().
 *
 * 11-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Moved thread reply port to cproc structure in cthread_internals.h,
 *	because mig calls are made while cproc is idle (no cthread structure).
 *	Changed cproc_switch() and cproc_start (COROUTINE implementation)
 *	to use address of saved context, rather than address of enclosing cproc,
 *	to eliminate dependency on cproc layout.
 */
/*
 * 	File: 	cprocs.c
 *	Author: Eric Cooper, Carnegie Mellon University
 *	Date:	Aug, 1987
 *
 * 	Implementation of cprocs (lightweight processes)
 * 	and primitive synchronization operations.
 */


#include <cthreads.h>
#include "cthread_internals.h"
#include <mach/message.h>
#include <hurd/threadvar.h>	/* GNU */
#include <assert-backtrace.h>

/*
 * Port_entry's are used by cthread_mach_msg to store information
 * about each port/port_set for which it is managing threads
 */

typedef struct port_entry {
	struct port_entry *next;	/* next port_entry */
	mach_port_t	port;		/* which port/port_set */
	struct cthread_queue queue;	/* queue of runnable threads for
					   this port/port_set */
	int min;			/* minimum number of kernel threads
					   to be used by this port/port_set */
	int max;			/* maximum number of kernel threads
					   to be used by this port/port_set */
	int held;			/* actual number of kernel threads
					   currentlt in use */
	spin_lock_t lock;		/* lock governing all above fields */
} *port_entry_t;

#define PORT_ENTRY_NULL ((port_entry_t) 0)

/* Available to outside for statistics */

int cthread_wait_stack_size = 8192;	/* stack size for idle threads */
int cthread_max_kernel_threads = 0;	/* max kernel threads */
int cthread_kernel_threads = 0;		/* current kernel threads */
private spin_lock_t n_kern_lock = SPIN_LOCK_INITIALIZER;
					/* lock for 2 above */
#ifdef STATISTICS
int cthread_ready = 0;			/* currently runnable */
int cthread_running = 1;		/* currently running */
int cthread_waiting = 0;		/* currently waiting */
int cthread_wired = 0;			/* currently wired */
private spin_lock_t wired_lock = SPIN_LOCK_INITIALIZER;
					/* lock for above */
int cthread_wait_stacks = 0;		/* total cthread waiting stacks */
int cthread_waiters = 0;		/* total of watiers */
int cthread_wakeup = 0;			/* total times woken when starting to
					   block */
int cthread_blocked = 0;		/* total blocked */
int cthread_rnone = 0;			/* total times no cthread available
					   to meet minimum for port_entry */
int cthread_yields = 0;			/* total cthread_yields */
int cthread_none = 0;			/* total idle wakeups w/o runnable */
int cthread_switches = 0;		/* total number of cproc_switches */
int cthread_no_mutex = 0;		/* total number times woken to get
					   mutex and couldn't */
private spin_lock_t mutex_count_lock = SPIN_LOCK_INITIALIZER;
					/* lock for above */
#endif  /* STATISTICS */

cproc_t cproc_list = NO_CPROC;		/* list of all cprocs */
private spin_lock_t cproc_list_lock = SPIN_LOCK_INITIALIZER;
					/* lock for above */
private int cprocs_started = FALSE;	/* initialized? */
private struct cthread_queue ready = QUEUE_INITIALIZER;
					/* ready queue */
private int ready_count = 0;		/* number of ready threads on ready
					   queue - number of messages sent */
private spin_lock_t ready_lock = SPIN_LOCK_INITIALIZER;
					/* lock for 2 above */
private mach_port_t wait_port = MACH_PORT_NULL;
					/* port on which idle threads wait */
private int wait_count = 0;		/* number of waiters - messages pending
					   to wake them */
private struct cthread_queue waiters = QUEUE_INITIALIZER;
					/* queue of cthreads to run as idle */
private spin_lock_t waiters_lock = SPIN_LOCK_INITIALIZER;
					/* lock for 2 above */
private port_entry_t port_list = PORT_ENTRY_NULL;
					/* master list of port_entries */
private spin_lock_t port_lock = SPIN_LOCK_INITIALIZER;
					/* lock for above queue */
private mach_msg_header_t wakeup_msg;	/* prebuilt message used by idle
					   threads */

/*
 * Return current value for max kernel threads
 * Note: 0 means no limit
 */
int
cthread_kernel_limit(void)
{
	return cthread_max_kernel_threads;
}

/*
 * Set max number of kernel threads
 * Note:	This will not currently terminate existing threads
 * 		over maximum.
 */

void
cthread_set_kernel_limit(int n)
{
	cthread_max_kernel_threads = n;
}

/*
 * Wire a cthread to its current kernel thread
 */

void
cthread_wire(void)
{
	register cproc_t p = cproc_self();

	/* In GNU, we wire all threads on creation (in cproc_alloc).  */
	assert_backtrace (p->wired != MACH_PORT_NULL);
}

/*
 * Unwire a cthread.  Deallocate its wait port.
 */

void
cthread_unwire(void)
{
	register cproc_t p = cproc_self();

	/* This is bad juju in GNU, where all cthreads must be wired.  */
	abort();
#if 0
	if (p->wired != MACH_PORT_NULL) {
		MACH_CALL(mach_port_mod_refs(mach_task_self(), p->wired,
					     MACH_PORT_RIGHT_SEND, -1), r);
		MACH_CALL(mach_port_mod_refs(mach_task_self(), p->wired,
					     MACH_PORT_RIGHT_RECEIVE, -1), r);
		p->wired = MACH_PORT_NULL;
#ifdef STATISTICS
		spin_lock(&wired_lock);
		cthread_wired--;
		spin_unlock(&wired_lock);
#endif  /* STATISTICS */
	}
#endif
}

private cproc_t
cproc_alloc(void)
{
	register cproc_t p = (cproc_t) malloc(sizeof(struct cproc));
	kern_return_t r;

	p->incarnation = NO_CTHREAD;
#if 0
	/* This member is not used in GNU.  */
	p->reply_port = MACH_PORT_NULL;
#endif

	spin_lock_init(&p->lock);
	p->state = CPROC_RUNNING;
	p->busy = 0;

	/*
	 * In GNU, every cthread must be wired.  So we just
	 * initialize P->wired on creation.
	 *
	 * A wired thread has a port associated with it for all
	 * of its wait/block cases.  We also prebuild a wakeup
	 * message.
	 */

	MACH_CALL(mach_port_allocate(mach_task_self(),
				     MACH_PORT_RIGHT_RECEIVE,
				     &p->wired), r);
	MACH_CALL(mach_port_insert_right(mach_task_self(),
					 p->wired, p->wired,
					 MACH_MSG_TYPE_MAKE_SEND), r);
	p->msg.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	p->msg.msgh_size = 0; /* initialized in call */
	p->msg.msgh_remote_port = p->wired;
	p->msg.msgh_local_port = MACH_PORT_NULL;
	p->msg.msgh_kind = MACH_MSGH_KIND_NORMAL;
	p->msg.msgh_id = 0;

	spin_lock(&cproc_list_lock);
	p->list = cproc_list;
	cproc_list = p;
	spin_unlock(&cproc_list_lock);

	return p;
}

/*
 * Called by cthread_init to set up initial data structures.
 */

vm_offset_t
cproc_init(void)
{
	kern_return_t r;

	cproc_t p = cproc_alloc();

	cthread_kernel_threads = 1;

	MACH_CALL(mach_port_allocate(mach_task_self(),
				     MACH_PORT_RIGHT_RECEIVE,
				     &wait_port), r);
	MACH_CALL(mach_port_insert_right(mach_task_self(),
					 wait_port, wait_port,
					 MACH_MSG_TYPE_MAKE_SEND), r);

	wakeup_msg.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	wakeup_msg.msgh_size = 0; /* initialized in call */
	wakeup_msg.msgh_remote_port = wait_port;
	wakeup_msg.msgh_local_port = MACH_PORT_NULL;
	wakeup_msg.msgh_kind = MACH_MSGH_KIND_NORMAL;
	wakeup_msg.msgh_id = 0;

	cprocs_started = TRUE;


	/*
	 * We pass back the new stack which should be switched to
	 * by crt0.  This guarantess correct size and alignment.
	 */
	return (stack_init(p));
}

/*
 * Insert cproc on ready queue.  Make sure it is ready for queue by
 * synching on its lock.  Just send message to wired cproc.
 */

private boolean_t cproc_ready(register cproc_t p, register int preq)
{
	register cproc_t s=cproc_self();
	kern_return_t r;

	if (p->wired != MACH_PORT_NULL) {
		r = mach_msg(&p->msg, MACH_SEND_MSG,
			     sizeof p->msg, 0, MACH_PORT_NULL,
			     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
#ifdef CHECK_STATUS
		if (r != MACH_MSG_SUCCESS) {
			mach_error("mach_msg", r);
			exit(1);
		}
#endif  /* CHECK_STATUS */
		return TRUE;
	}
	spin_lock(&p->lock);		/* is it ready to be queued?  It
					   can appear on a queue before
					   being switched from.  This lock
					   is released by cproc_switch as
					   its last operation. */
	if (p->state & CPROC_SWITCHING) {
		/*
		 * We caught it early on.  Just set to RUNNING
		 * and we will save a lot of time.
		 */
		p->state = (p->state & ~CPROC_SWITCHING) | CPROC_RUNNING;
		spin_unlock(&p->lock);
		return TRUE;
	}
	spin_unlock(&p->lock);

	spin_lock(&ready_lock);

	if (preq) {
		cthread_queue_preq(&ready, p);
	} else {
		cthread_queue_enq(&ready, p);
	}
#ifdef STATISTICS
	cthread_ready++;
#endif  /* STATISTICS */
	ready_count++;

	if ((s->state & CPROC_CONDWAIT) && !(s->wired)) {
		/*
		 * This is an optimiztion.  Don't bother waking anyone to grab
		 * this guy off the ready queue since my thread will block
		 * momentarily for the condition wait.
		 */

		spin_unlock(&ready_lock);
		return TRUE;
	}

	if ((ready_count > 0) && wait_count) {
		wait_count--;
		ready_count--;
		spin_unlock(&ready_lock);
		r = mach_msg(&wakeup_msg, MACH_SEND_MSG,
			     sizeof wakeup_msg, 0, MACH_PORT_NULL,
			     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
#ifdef CHECK_STATUS
		if (r != MACH_MSG_SUCCESS) {
			mach_error("mach_msg", r);
			exit(1);
		}
#endif  /* CHECK_STATUS */
		return TRUE;
	}
	spin_unlock(&ready_lock);
	return FALSE;
}

/*
 * This is only run on a partial "waiting" stack and called from
 * cproc_start_wait
 */

void
cproc_waiting(cproc_t p)
{
	mach_msg_header_t msg;
	register cproc_t new;
	kern_return_t r;

#ifdef STATISTICS
	spin_lock(&ready_lock);
	cthread_waiting++;
	cthread_waiters++;
	spin_unlock(&ready_lock);
#endif  /* STATISTICS */
	for (;;) {
		MACH_CALL(mach_msg(&msg, MACH_RCV_MSG,
				   0, sizeof msg, wait_port,
				   MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL), r);
		spin_lock(&ready_lock);
		cthread_queue_deq(&ready, cproc_t, new);
		if (new != NO_CPROC) break;
		wait_count++;
		ready_count++;
#ifdef STATISTICS
		cthread_none++;
#endif  /* STATISTICS */
		spin_unlock(&ready_lock);
	}
#ifdef STATISTICS
	cthread_ready--;
	cthread_running++;
	cthread_waiting--;
#endif  /* STATISTICS */
	spin_unlock(&ready_lock);
	spin_lock(&new->lock);
	new->state = CPROC_RUNNING;
	spin_unlock(&new->lock);
	spin_lock(&waiters_lock);
	cthread_queue_enq(&waiters, p);
	spin_lock(&p->lock);
	spin_unlock(&waiters_lock);
	cproc_switch(&p->context,&new->context,&p->lock);
}

/*
 * Get a waiter with stack
 *
 */

private cproc_t
cproc_waiter(void)
{
	register cproc_t waiter;

	spin_lock(&waiters_lock);
	cthread_queue_deq(&waiters, cproc_t, waiter);
	spin_unlock(&waiters_lock);
	if (waiter == NO_CPROC) {
		vm_address_t base;
		kern_return_t r;
#ifdef STATISTICS
		spin_lock(&waiters_lock);
		cthread_wait_stacks++;
		spin_unlock(&waiters_lock);
#endif  /* STATISTICS */
		waiter = cproc_alloc();
		MACH_CALL(vm_allocate(mach_task_self(), &base,
				      cthread_wait_stack_size, TRUE), r);
		waiter->stack_base = base;
		waiter->stack_size = cthread_wait_stack_size;
	}
	return (waiter);
}


/*
 * Current cproc is blocked so switch to any ready cprocs, or, if
 * none, go into the wait state.
 *
 * You must hold cproc_self()->lock when called.
 */

void
cproc_block(void)
{
  extern unsigned int __hurd_threadvar_max; /* GNU */
	register cproc_t waiter, new, p = cproc_self();

	if (p->wired != MACH_PORT_NULL) {
		mach_msg_header_t msg;
		kern_return_t r;

		spin_unlock(&p->lock);
		MACH_CALL(mach_msg(&msg, MACH_RCV_MSG,
				   0, sizeof msg, p->wired,
				   MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL), r);
		return;
	}
	p->state = CPROC_SWITCHING;
	spin_unlock(&p->lock);
	spin_lock(&ready_lock);
#ifdef STATISTICS
	cthread_blocked++;
#endif  /* STATISTICS */
	cthread_queue_deq(&ready, cproc_t, new);
	if (new) {
#ifdef STATISTICS
		cthread_ready--;
		cthread_switches++;
#endif  /* STATISTICS */
		ready_count--;
		spin_unlock(&ready_lock);
		spin_lock(&p->lock);
		if (p->state == CPROC_RUNNING) { /* have we been saved */
			spin_unlock(&p->lock);
#ifdef STATISTICS
			spin_lock(&ready_lock);
			cthread_wakeup++;
			cthread_switches--;
			spin_unlock(&ready_lock);
#endif  /* STATISTICS */
			cproc_ready(new, 1);  /* requeue at head were it was */
		} else {
			p->state = CPROC_BLOCKED;
			spin_lock(&new->lock); /* incase still switching */
			new->state = CPROC_RUNNING;
			spin_unlock(&new->lock);
			cproc_switch(&p->context,&new->context,&p->lock);
		}
	} else {
		wait_count++;
#ifdef STATISTICS
		cthread_running--;
#endif  /* STATISTICS */
		spin_unlock(&ready_lock);
		waiter = cproc_waiter();
		spin_lock(&p->lock);
		if (p->state == CPROC_RUNNING) { /* we have been saved */
			spin_unlock(&p->lock);
			spin_lock(&ready_lock);
			wait_count--;
#ifdef STATISTICS
			cthread_running++;
			cthread_wakeup++;
#endif  /* STATISTICS */
			spin_unlock(&ready_lock);
			spin_lock(&waiters_lock);
			cthread_queue_preq(&waiters, waiter);
			spin_unlock(&waiters_lock);
		} else {
			p->state = CPROC_BLOCKED;
			spin_lock(&waiter->lock); /* in case still switching */
			spin_unlock(&waiter->lock);
			cproc_start_wait
			  (&p->context, waiter,
			   cproc_stack_base(waiter,
					    sizeof(ur_cthread_t *) +
					    /* Account for GNU per-thread
					       variables.  */
					    __hurd_threadvar_max *
					    sizeof (long int)),
			   &p->lock);
		}
	}
}

/*
 * Implement C threads using MACH threads.
 */
cproc_t
cproc_create(void)
{
	register cproc_t child = cproc_alloc();
	register kern_return_t r;
	extern void cproc_setup();
	extern void cproc_prepare();
	extern void cthread_body();
	thread_t n;

      	alloc_stack(child);
	spin_lock(&n_kern_lock);
	if (cthread_max_kernel_threads == 0 ||
	    cthread_kernel_threads < cthread_max_kernel_threads) {
		tcbhead_t *tcb = _dl_allocate_tls(NULL);
		cthread_kernel_threads++;
		spin_unlock(&n_kern_lock);
		MACH_CALL(thread_create(mach_task_self(), &n), r);
		cproc_setup(child, n, tcb, cthread_body);	/* machine dependent */
		MACH_CALL(thread_resume(n), r);
#ifdef STATISTICS
		spin_lock(&ready_lock);
		cthread_running++;
		spin_unlock(&ready_lock);
#endif  /* STATISTICS */
	} else {
	  	vm_offset_t stack;
		spin_unlock(&n_kern_lock);
		child->state = CPROC_BLOCKED;
		/* The original CMU code does the excessively clever
		   optimization of putting CHILD at the base of the stack
		   and setting up to be the argument to cthread_body in the
		   same place (by passing zero as the second arg to
		   cproc_stack_base here)..  This doesn't fly for GNU,
		   because we need some more space allocated at the base of
		   the stack, after the cproc_self pointer (where CHILD is
		   stored).  */
		stack = cproc_stack_base(child,
					 sizeof(ur_cthread_t *) +
					 /* Account for GNU per-thread
					    variables.  */
					 __hurd_threadvar_max *
					 sizeof (long int));
		cproc_prepare(child, &child->context, stack, &cthread_body);
		/* Set up the cproc_self ptr at the base of CHILD's stack.  */
		ur_cthread_ptr(stack) = (ur_cthread_t) child;
		cproc_ready(child,0);
	}
	return child;
}

void
condition_wait(condition_t c, mutex_t m)
{
	register cproc_t p = cproc_self();

	p->state = CPROC_CONDWAIT | CPROC_SWITCHING;

	spin_lock(&c->lock);
	cthread_queue_enq(&c->queue, p);
	spin_unlock(&c->lock);
#ifdef	WAIT_DEBUG
	p->waiting_for = (char *)c;
#endif	 /* WAIT_DEBUG */

	mutex_unlock(m);

	spin_lock(&p->lock);
	if (p->state & CPROC_SWITCHING) {
		cproc_block();
	} else {
		p->state = CPROC_RUNNING;
		spin_unlock(&p->lock);
	}


#ifdef	WAIT_DEBUG
	p->waiting_for = (char *)0;
#endif	 /* WAIT_DEBUG */

	/*
	 * Re-acquire the mutex and return.
	 */
	mutex_lock(m);
}

/* Declare that IMPLICATOR should consider IMPLICATAND's waiter queue
   to be an extension of its own queue.  It is an error for either
   condition to be deallocated as long as the implication persists. */
void
condition_implies (condition_t implicator, condition_t implicatand)
{
  struct cond_imp *imp;

  imp = malloc (sizeof (struct cond_imp));
  imp->implicatand = implicatand;
  imp->next = implicator->implications;
  implicator->implications = imp;
}

/* Declare that the implication relationship from IMPLICATOR to
   IMPLICATAND should cease. */
void
condition_unimplies (condition_t implicator, condition_t implicatand)
{
  struct cond_imp **impp;

  for (impp = &implicator->implications; *impp; impp = &(*impp)->next)
    {
      if ((*impp)->implicatand == implicatand)
	{
	  struct cond_imp *tmp = *impp;
	  *impp = (*impp)->next;
	  free (tmp);
	  return;
	}
    }
}

/* Signal one waiter on C.  If there were no waiters at all, return
   0, else return 1. */
int
cond_signal(condition_t c)
{
	register cproc_t p;
	struct cond_imp *imp;

	spin_lock(&c->lock);
	cthread_queue_deq(&c->queue, cproc_t, p);
	spin_unlock(&c->lock);
	if (p != NO_CPROC) {
		cproc_ready(p,0);
		return 1;
	}
	else {
		for (imp = c->implications; imp; imp = imp->next)
			if (cond_signal (imp->implicatand))
				return 1;
        }
	return 0;
}

void
cond_broadcast(condition_t c)
{
	register cproc_t p;
	struct cthread_queue blocked_queue;
	struct cond_imp *imp;

	cthread_queue_init(&blocked_queue);

	spin_lock(&c->lock);
	for (;;) {
		cthread_queue_deq(&c->queue, cproc_t, p);
		if (p == NO_CPROC)
			break;
		cthread_queue_enq(&blocked_queue, p);
	}
	spin_unlock(&c->lock);

	for(;;) {
		cthread_queue_deq(&blocked_queue, cproc_t, p);
		if (p == NO_CPROC)
			break;
		cproc_ready(p,0);
	}

	for (imp = c->implications; imp; imp = imp->next)
		condition_broadcast (imp->implicatand);
}

void
cthread_yield(void)
{
	register cproc_t new, p = cproc_self();

	if (p->wired != MACH_PORT_NULL) {
		yield();
		return;
	}
	spin_lock(&ready_lock);
#ifdef STATISTICS
	cthread_yields++;
#endif  /* STATISTICS */
	cthread_queue_deq(&ready, cproc_t, new);
	if (new) {
		cthread_queue_enq(&ready, p);
		spin_lock(&p->lock);
		p->state = CPROC_BLOCKED;
		spin_unlock(&ready_lock);
		spin_lock(&new->lock);
		new->state = CPROC_RUNNING;
		spin_unlock(&new->lock);
		cproc_switch(&p->context,&new->context,&p->lock);
	} else {
		spin_unlock(&ready_lock);
		yield();
	}
}

/*
 * Mutex objects.
 */

void
__mutex_lock_solid(void *ptr)
{
	register mutex_t m = ptr;
	register cproc_t p = cproc_self();
	register int queued;
	register int tried = 0;

#ifdef	WAIT_DEBUG
	p->waiting_for = (char *)m;
#endif	 /* WAIT_DEBUG */
	while (1) {
		spin_lock(&m->lock);
		if (cthread_queue_head(&m->queue, cproc_t) == NO_CPROC) {
			cthread_queue_enq(&m->queue, p);
			queued = 1;
		} else {
			queued = 0;
		}
		if (spin_try_lock(&m->held)) {
			if (queued) cthread_queue_deq(&m->queue, cproc_t, p);
			spin_unlock(&m->lock);
#ifdef	WAIT_DEBUG
			p->waiting_for = (char *)0;
#endif	 /* WAIT_DEBUG */
			return;
		} else {
			if (!queued) cthread_queue_enq(&m->queue, p);
			spin_lock(&p->lock);
			spin_unlock(&m->lock);
			cproc_block();
			if (spin_try_lock(&m->held)) {
#ifdef	WAIT_DEBUG
			    p->waiting_for = (char *)0;
#endif	 /* WAIT_DEBUG */
			    return;
			}
#ifdef STATISTICS
			spin_lock(&mutex_count_lock);
			cthread_no_mutex++;
			spin_unlock(&mutex_count_lock);
#endif  /* STATISTICS */
		}
	}
}

void
__mutex_unlock_solid(void *ptr)
{
	register mutex_t m = ptr;
	register cproc_t new;

	if (!spin_try_lock(&m->held))
		return;
	spin_lock(&m->lock);
	cthread_queue_deq(&m->queue, cproc_t, new);
	spin_unlock(&m->held);
	spin_unlock(&m->lock);
	if (new) {
		cproc_ready(new,0);
	}
}


/*
 * Use instead of mach_msg in a multi-threaded server so as not
 * to tie up excessive kernel threads.  This uses a simple linked list for
 * ports since this should never be more than a few.
 */

/*
 * A cthread holds a reference to a port_entry even after it receives a
 * message.  This reference is not released until the thread does a
 * cthread_msg_busy.  This allows the fast case of a single mach_msg
 * call to occur as often as is possible.
 */

private port_entry_t
get_port_entry(mach_port_t port, int min, int max)
{
	register port_entry_t i;

	spin_lock(&port_lock);
	for(i=port_list;i!=PORT_ENTRY_NULL;i=i->next)
		if (i->port == port) {
			spin_unlock(&port_lock);
			return i;
		}
	i = (port_entry_t)malloc(sizeof(struct port_entry));
	cthread_queue_init(&i->queue);
	i->port = port;
	i->next = port_list;
	port_list = i;
	i->min = min;
	i->max = max;
	i->held = 0;
	spin_lock_init(&i->lock);
	spin_unlock(&port_lock);
	return i;
}

void
cthread_msg_busy(mach_port_t port, int min, int max)
{
	register port_entry_t port_entry;
	register cproc_t new, p = cproc_self();

	if (p->busy) {
	    port_entry = get_port_entry(port, min, max);
	    spin_lock(&port_entry->lock);
	    p->busy = 0;
	    if (port_entry->held <= port_entry->min) {
		cthread_queue_deq(&port_entry->queue, cproc_t, new);
		if (new != NO_CPROC){
		    spin_unlock(&port_entry->lock);
		    cproc_ready(new,0);
		} else {
		    port_entry->held--;
		    spin_unlock(&port_entry->lock);
#ifdef STATISTICS
		    spin_lock(&port_lock);
		    cthread_rnone++;
		    spin_unlock(&port_lock);
#endif  /* STATISTICS */
		}
	    } else {
		port_entry->held--;
		spin_unlock(&port_entry->lock);
	    }
	}

}

void
cthread_msg_active(mach_port_t port, int min, int max)
{
	register cproc_t p = cproc_self();
	register port_entry_t port_entry;

	if (!p->busy) {
	    port_entry = get_port_entry(port, min, max);
	    if (port_entry == 0) return;
	    spin_lock(&port_entry->lock);
	    if (port_entry->held < port_entry->max) {
		port_entry->held++;
		p->busy = port_entry;
	    }
	    spin_unlock(&port_entry->lock);
	}
}

mach_msg_return_t
cthread_mach_msg(register mach_msg_header_t *header,
		 register mach_msg_option_t option, mach_msg_size_t send_size,
		 mach_msg_size_t rcv_size, register mach_port_t rcv_name,
		 mach_msg_timeout_t timeout, mach_port_t notify, int min,
		 int max)
{
	register port_entry_t port_entry;
	register cproc_t p = cproc_self();
	register int sent=0;
	mach_msg_return_t r;
	port_entry_t op = (port_entry_t)p->busy;

	port_entry = get_port_entry(rcv_name, min, max);

	if (op && (port_entry_t)op != port_entry)
	    cthread_msg_busy(op->port, op->min, op->max);
	spin_lock(&port_entry->lock);
	if (!(port_entry == (port_entry_t)p->busy)) {
	    if (port_entry->held >= max) {
		if (option & MACH_SEND_MSG) {
		    spin_unlock(&port_entry->lock);
		    r = mach_msg(header, option &~ MACH_RCV_MSG,
				 send_size, 0, MACH_PORT_NULL,
				 timeout, notify);
		    if (r != MACH_MSG_SUCCESS) return r;
		    spin_lock(&port_entry->lock);
		    sent=1;
		}
		if (port_entry->held >= max) {
		    spin_lock(&p->lock);
		    cthread_queue_preq(&port_entry->queue, p);
		    spin_unlock(&port_entry->lock);
#ifdef	WAIT_DEBUG
		    p->waiting_for = (char *)port_entry;
#endif	 /* WAIT_DEBUG */
		    cproc_block();
		} else {
		    port_entry->held++;
		    spin_unlock(&port_entry->lock);
		}
	    } else {
		port_entry->held++;
		spin_unlock(&port_entry->lock);
	    }
	} else {
		spin_unlock(&port_entry->lock);
	}
#ifdef	WAIT_DEBUG
	p->waiting_for = (char *)0;
#endif	 /* WAIT_DEBUG */
	p->busy = port_entry;
	if ((option & MACH_SEND_MSG) && !sent) {
	    r = mach_msg(header, option,
			 send_size, rcv_size, rcv_name,
			 timeout, notify);
	} else {
	    r = mach_msg(header, option &~ MACH_SEND_MSG,
			 0, rcv_size, rcv_name,
			 timeout, notify);
	}
	return r;
}

void
cproc_fork_prepare(void)
{
    register cproc_t p = cproc_self();

    vm_inherit(mach_task_self(),p->stack_base, p->stack_size, VM_INHERIT_COPY);
    spin_lock(&port_lock);
    spin_lock(&cproc_list_lock);
}

void
cproc_fork_parent(void)
{
    register cproc_t p = cproc_self();

    spin_unlock(&cproc_list_lock);
    spin_unlock(&port_lock);
    vm_inherit(mach_task_self(),p->stack_base, p->stack_size, VM_INHERIT_NONE);
}

void
cproc_fork_child(void)
{
    register cproc_t l,p = cproc_self();
    cproc_t m;
    register port_entry_t pe;
    port_entry_t pet;
    kern_return_t r;


    vm_inherit(mach_task_self(),p->stack_base, p->stack_size, VM_INHERIT_NONE);
    spin_lock_init(&n_kern_lock);
    cthread_kernel_threads=0;
#ifdef STATISTICS
    cthread_ready = 0;
    cthread_running = 1;
    cthread_waiting = 0;
    cthread_wired = 0;
    spin_lock_init(&wired_lock);
    cthread_wait_stacks = 0;
    cthread_waiters = 0;
    cthread_wakeup = 0;
    cthread_blocked = 0;
    cthread_rnone = 0;
    cthread_yields = 0;
    cthread_none = 0;
    cthread_switches = 0;
    cthread_no_mutex = 0;
    spin_lock_init(&mutex_count_lock);
#endif  /* STATISTICS */

    for(l=cproc_list;l!=NO_CPROC;l=m) {
	m=l->next;
	if (l!=p)
	    free(l);
    }

    cproc_list = p;
    p->next = NO_CPROC;
    spin_lock_init(&cproc_list_lock);
    cprocs_started = FALSE;
    cthread_queue_init(&ready);
    ready_count = 0;
    spin_lock_init(&ready_lock);

    MACH_CALL(mach_port_allocate(mach_task_self(),
				 MACH_PORT_RIGHT_RECEIVE,
				 &wait_port), r);
    MACH_CALL(mach_port_insert_right(mach_task_self(),
				     wait_port, wait_port,
				     MACH_MSG_TYPE_MAKE_SEND), r);
    wakeup_msg.msgh_remote_port = wait_port;
    wait_count = 0;
    cthread_queue_init(&waiters);
    spin_lock_init(&waiters_lock);
    for(pe=port_list;pe!=PORT_ENTRY_NULL;pe=pet) {
	pet = pe->next;
	free(pe);
    }
    port_list = PORT_ENTRY_NULL;
    spin_lock_init(&port_lock);

    if (p->wired) cthread_wire();
}
