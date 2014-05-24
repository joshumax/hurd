/*
 * Mach Operating System
 * Copyright (c) 1992,1991,1990,1989 Carnegie Mellon University
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
 * 20-Oct-93  Tero Kivinen (kivinen) at Helsinki University of Technology
 *	Renamed cthread_t->catch to to cthread_t->catch_exit, because
 *	catch is reserved word in c++.
 *
 * $Log: cthreads.c,v $
 * Revision 1.15  2002/07/31 02:35:14  marcus
 * Add comment to last change, for the benefit of the next merge :)
 *
 * Revision 1.14  2002/07/31 02:20:44  marcus
 * 2002-07-29  Marcus Brinkmann  <marcus@gnu.org>
 *
 * 	* cthreads.c (cthread_init): Move cthread_alloc call before
 * 	cproc_init call (lost in merge).
 *
 * Revision 1.13  2002/05/28 23:55:55  roland
 * 2002-05-28  Roland McGrath  <roland@frob.com>
 *
 * 	* cthreads.c (cthread_fork_prepare, cthread_fork_parent,
 * 	cthread_fork_child): Don't call malloc_fork_* (lost in merge).
 *
 * Revision 1.12  2002/05/27 02:50:10  roland
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
 * Revision 2.13  93/01/21  12:27:55  danner
 * 	Remove deadlock in cproc_fork_child; must release malloc lock first.
 * 	[93/01/19  16:37:43  bershad]
 *
 * Revision 2.12  93/01/14  18:05:00  danner
 * 	Converted file to ANSI C.
 * 	Removed use of obsolete type any_t.
 * 	[92/12/18            pds]
 * 	64bit cleanup.
 * 	[92/12/01            af]
 *
 * 	Free private_data in cthread_exit, from Mike Kupfer.
 * 	[92/11/30            af]
 *
 * Revision 2.11  92/07/20  13:33:37  cmaeda
 * 	In cthread_init, do machine dependent initialization if it's defined.
 * 	[92/05/11  14:41:08  cmaeda]
 *
 * Revision 2.10  91/08/28  11:19:26  jsb
 * 	Fixed mig_init initialization in cthread_fork_child.
 * 	[91/08/23            rpd]
 *
 * Revision 2.9  91/07/31  18:34:23  dbg
 * 	Fix bad self-pointer reference.
 *
 * 	Don't declare _setjmp and _longjmp; they are included by
 * 	cthreads.h.
 * 	[91/07/30  17:33:50  dbg]
 *
 * Revision 2.8  91/05/14  17:56:31  mrt
 * 	Correcting copyright
 *
 * Revision 2.7  91/02/14  14:19:47  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:07  mrt]
 *
 * Revision 2.6  90/11/05  14:37:03  rpd
 * 	Added cthread_fork_{prepare,parent,child}.
 * 	[90/11/02            rwd]
 *
 * 	Add spin_lock_t.
 * 	[90/10/31            rwd]
 *
 * Revision 2.5  90/08/07  14:30:58  rpd
 * 	Removed RCS keyword nonsense.
 *
 * Revision 2.4  90/06/02  15:13:49  rpd
 * 	Converted to new IPC.
 * 	[90/03/20  20:56:44  rpd]
 *
 * Revision 2.3  90/01/19  14:37:12  rwd
 * 	Make cthread_init return pointer to new stack.
 * 	[89/12/18  19:17:45  rwd]
 *
 * Revision 2.2  89/12/08  19:53:37  rwd
 * 	Change cproc and cthread counters to globals with better names.
 * 	[89/11/02            rwd]
 *
 * Revision 2.1  89/08/03  17:09:34  rwd
 * Created.
 *
 *
 * 31-Dec-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed cthread_exit() logic for the case of the main thread,
 *	to fix thread and stack memory leak found by Camelot group.
 *
 * 21-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Added consistency check in beginning of cthread_body().
 *
 * 11-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Removed cthread_port() and cthread_set_port().
 *	Removed port deallocation from cthread_free().
 *	Minor changes to cthread_body(), cthread_exit(), and cthread_done().
 *
 * 10-Aug-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed call to mig_init() in cthread_init() to pass 1 as argument.
 *
 * 31-Jul-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Added call to mig_init() from cthread_init().
 */
/*
 * 	File:	cthreads.c
 *	Author:	Eric Cooper, Carnegie Mellon University
 *	Date:	July, 1987
 *
 * 	Implementation of fork, join, exit, etc.
 */

#include <cthreads.h>
#include <mach/mig_support.h>
#include "cthread_internals.h"
#include <locale.h>

/*
 * Thread status bits.
 */
#define	T_MAIN		0x1
#define	T_RETURNED	0x2
#define	T_DETACHED	0x4

#if	defined(DEBUG)
int cthread_debug = FALSE;
#endif	/* defined(DEBUG) */

private struct cthread_queue cthreads = QUEUE_INITIALIZER;
private struct mutex cthread_lock = MUTEX_INITIALIZER;
private struct condition cthread_needed = CONDITION_INITIALIZER;
private struct condition cthread_idle = CONDITION_INITIALIZER;
int cthread_cprocs = 0;
int cthread_cthreads = 0;
int cthread_max_cprocs = 0;

private cthread_t free_cthreads = NO_CTHREAD;		/* free list */
private spin_lock_t free_lock = SPIN_LOCK_INITIALIZER;	/* unlocked */

private struct cthread initial_cthread = { 0 };

private cthread_t
cthread_alloc(cthread_fn_t func, void *arg)
{
	register cthread_t t = NO_CTHREAD;

	if (free_cthreads != NO_CTHREAD) {
		/*
		 * Don't try for the lock unless
		 * the list is likely to be nonempty.
		 * We can't be sure, though, until we lock it.
		 */
		spin_lock(&free_lock);
		t = free_cthreads;
		if (t != NO_CTHREAD)
			free_cthreads = t->next;
		spin_unlock(&free_lock);
	}
	if (t == NO_CTHREAD) {
		/*
		 * The free list was empty.
		 * We may have only found this out after
		 * locking it, which is why this isn't an
		 * "else" branch of the previous statement.
		 */
		t = (cthread_t) malloc(sizeof(struct cthread));
	}
	*t = initial_cthread;
	t->func = func;
	t->arg = arg;
	return t;
}

private void
cthread_free(cthread_t t)
{
	spin_lock(&free_lock);
	t->next = free_cthreads;
	free_cthreads = t;
	spin_unlock(&free_lock);
}

vm_offset_t
cthread_init(void)
{
	static int cthreads_started = FALSE;
	register cproc_t p;
	register cthread_t t;
	vm_offset_t stack;

	if (cthreads_started)
		return 0;

	/* cthread_alloc must be called before cproc_init, because
	   malloc is not usable between initializing the new stack and
	   switching to it.  */
	t = cthread_alloc((cthread_fn_t) 0, (any_t) 0);
	stack = cproc_init();
	cthread_cprocs = 1;

#ifdef cthread_md_init
	cthread_md_init();
#endif

	cthread_cthreads = 1;
	t->state |= T_MAIN;
	cthread_set_name(t, "main");

	/* cproc_self() doesn't work yet, because
	   we haven't yet switched to the new stack. */

	p = *(cproc_t *)&ur_cthread_ptr(stack);
	p->incarnation = t;
	/* The original CMU code passes P to mig_init.  In GNU, mig_init
	   does not know about cproc_t; instead it expects to be passed the
	   stack pointer of the initial thread.  */
	mig_init((void *) stack); /* enable multi-threaded mig interfaces */

	cthreads_started = TRUE;
	return stack;
}

/*
 * Used for automatic initialization by crt0.
 */
vm_offset_t (*_cthread_init_routine)(void) = cthread_init;


/*
 * Procedure invoked at the base of each cthread.
 */
void
cthread_body(cproc_t self)
{
	register cthread_t t;

	ASSERT(cproc_self() == self);
	TRACE(printf("[idle] cthread_body(%x)\n", self));
	mutex_lock(&cthread_lock);
	for (;;) {
		/*
		 * Dequeue a thread invocation request.
		 */
		cthread_queue_deq(&cthreads, cthread_t, t);
		if (t != NO_CTHREAD) {
			/*
			 * We have a thread to execute.
			 */
			mutex_unlock(&cthread_lock);
			cthread_assoc(self, t);		/* assume thread's identity */
			if (_setjmp(t->catch_exit) == 0) {	/* catch for cthread_exit() */
				/*
				 * Execute the fork request.
				 */

			        /* A fresh thread needs to be bound to the
				   global locale.  */
			  	uselocale (LC_GLOBAL_LOCALE);

				t->result = (*(t->func))(t->arg);
			}
			/*
			 * Return result from thread.
			 */
			TRACE(printf("[%s] done()\n", cthread_name(t)));
			mutex_lock(&t->lock);
			if (t->state & T_DETACHED) {
				mutex_unlock(&t->lock);
				cthread_free(t);
			} else {
				t->state |= T_RETURNED;
				mutex_unlock(&t->lock);
				condition_signal(&t->done);
			}
			cthread_assoc(self, NO_CTHREAD);
			mutex_lock(&cthread_lock);
			cthread_cthreads -= 1;
		} else {
			/*
			 * Queue is empty.
			 * Signal that we're idle in case the main thread
			 * is waiting to exit, then wait for reincarnation.
			 */
			condition_signal(&cthread_idle);
			condition_wait(&cthread_needed, &cthread_lock);
		}
	}
}

cthread_t
cthread_fork(cthread_fn_t func, void *arg)
{
	register cthread_t t;

	TRACE(printf("[%s] fork()\n", cthread_name(cthread_self())));
	mutex_lock(&cthread_lock);
	t = cthread_alloc(func, arg);
	cthread_queue_enq(&cthreads, t);
	if (++cthread_cthreads > cthread_cprocs && (cthread_max_cprocs == 0 || cthread_cprocs < cthread_max_cprocs)) {
		cthread_cprocs += 1;
		cproc_create();
	}
	mutex_unlock(&cthread_lock);
	condition_signal(&cthread_needed);
	return t;
}

void
cthread_detach(t)
	cthread_t t;
{
	TRACE(printf("[%s] detach(%s)\n", cthread_name(cthread_self()), cthread_name(t)));
	mutex_lock(&t->lock);
	if (t->state & T_RETURNED) {
		mutex_unlock(&t->lock);
		cthread_free(t);
	} else {
		t->state |= T_DETACHED;
		mutex_unlock(&t->lock);
	}
}

void *
cthread_join(cthread_t t)
{
	void *result;

	TRACE(printf("[%s] join(%s)\n", cthread_name(cthread_self()), cthread_name(t)));
	mutex_lock(&t->lock);
	ASSERT(! (t->state & T_DETACHED));
	while (! (t->state & T_RETURNED))
		condition_wait(&t->done, &t->lock);
	result = t->result;
	mutex_unlock(&t->lock);
	cthread_free(t);
	return result;
}

void
cthread_exit(void *result)
{
	register cthread_t t = cthread_self();

	TRACE(printf("[%s] exit()\n", cthread_name(t)));
	t->result = result;
	if (t->private_data != 0) {
		free((char *)t->private_data);
		t->private_data = 0;
	}
	if (t->state & T_MAIN) {
		mutex_lock(&cthread_lock);
		while (cthread_cthreads > 1)
			condition_wait(&cthread_idle, &cthread_lock);
		mutex_unlock(&cthread_lock);
		exit((int) (integer_t) result);
	} else {
		_longjmp(t->catch_exit, TRUE);
	}
}

/*
 * Used for automatic finalization by crt0.  Cast needed since too many C
 * compilers choke on the type void (*)().
 */
int (*_cthread_exit_routine)() = (int (*)()) cthread_exit;

void
cthread_set_name(cthread_t t, const char *name)
{
	t->name = name;
}

const char *
cthread_name(cthread_t t)
{
	return (t == NO_CTHREAD ? "idle" : (t->name == 0 ? "?" : t->name));
}

int
cthread_limit(void)
{
	return cthread_max_cprocs;
}

void
cthread_set_limit(int n)
{
	cthread_max_cprocs = n;
}

int
cthread_count(void)
{
	return cthread_cthreads;
}

void
cthread_fork_prepare(void)
{
    spin_lock(&free_lock);
    mutex_lock(&cthread_lock);
    cproc_fork_prepare();
}

void
cthread_fork_parent(void)
{
    cproc_fork_parent();
    mutex_unlock(&cthread_lock);
    spin_unlock(&free_lock);
}

void
cthread_fork_child(void)
{
    cthread_t t;
    cproc_t p;

    cproc_fork_child();
    mutex_unlock(&cthread_lock);
    spin_unlock(&free_lock);
    condition_init(&cthread_needed);
    condition_init(&cthread_idle);

    cthread_max_cprocs = 0;

    stack_fork_child();

    while (TRUE) {		/* Free cthread runnable list */
	cthread_queue_deq(&cthreads, cthread_t, t);
	if (t == NO_CTHREAD) break;
	free((char *) t);
    }

    while (free_cthreads != NO_CTHREAD) {	/* Free cthread free list */
	t = free_cthreads;
	free_cthreads = free_cthreads->next;
	free((char *) t);
    }

    cthread_cprocs = 1;
    t = cthread_self();
    cthread_cthreads = 1;
    t->state |= T_MAIN;
    cthread_set_name(t, "main");

    p = cproc_self();
    p->incarnation = t;
    /* XXX needs hacking for GNU */
    mig_init(p);		/* enable multi-threaded mig interfaces */
}
