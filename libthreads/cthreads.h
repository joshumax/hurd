/*
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990,1989 Carnegie Mellon University
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
 * 12-Oct-93  Johannes Helander (jvh) at Helsinki University of Technology
 *	Added CONDITION_NAMED_INITIALIZER and MUTEX_NAMED_INITIALIZER1
 * 	macros. They take one argument: a name string.
 *
 * $Log: cthreads.h,v $
 * Revision 1.19  2002/05/28 23:55:58  roland
 * 2002-05-28  Roland McGrath  <roland@frob.com>
 *
 * 	* cthreads.h (hurd_condition_wait, condition_implies,
 * 	condition_unimplies): Restore decls lost in merge.
 * 	(mutex_clear): Define as mutex_init instead of bogon (lost in merge).
 *
 * Revision 1.18  2002/05/27 02:50:10  roland
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
 * Revision 2.17  93/05/10  19:43:11  rvb
 * 	Removed include of stdlib.h and just define malloc
 * 	[93/04/27            mrt]
 *
 * Revision 2.16  93/05/10  17:51:26  rvb
 * 	Just imagine how much more useful TWO special/fast lookup
 * 	variables could be.  (Actually, I am planning on using this
 * 	for bsdss -- for multiple threads per task.  If I don't, I'll
 * 	remove the feature.)
 * 	[93/05/10            rvb]
 * 	Big mistake here! CTHREAD_DATA must always be set TRUE.
 * 	cthreads.h is included by import_mach.h by lots of files
 * 	that are not compiled with -DCTHREAD_DATA.  This means
 * 	they see a different structure for cthread_t than the
 * 	cthread library -- which is compiled with CTHREAD_DATA.
 * 	Also, make cthread_set_data and cthread_data macros.
 * 	[93/05/06            rvb]
 * 	Flush stdlib
 * 	[93/05/05            rvb]
 *
 * Revision 2.15  93/01/27  09:03:32  danner
 * 	Updated include of mach/mach.h to mach.h
 *
 *
 * Revision 2.14  93/01/24  13:24:50  danner
 * 	Get MACRO_BEGIN, MACRO_END, NEVER, ... from sys/macro_help.h
 * 	why define it here.
 * 	[92/10/20            rvb]
 *
 * Revision 2.13  93/01/14  18:05:04  danner
 * 	Added MACRO_BEGIN and MACRO_END to definition of spin_lock.
 * 	Fixed return value of cthread_set_data.
 * 	Added prototypes for other miscellaneous functions.
 * 	[92/12/18            pds]
 * 	Converted file to ANSI C.
 * 	Added declarations of cthread_fork_{prepare,parent,child}.
 * 	Added include of <sys/macro_help.h>.
 * 	[92/12/13            pds]
 *
 * 	Replaced calloc declaration with an include of stdlib.h.
 * 	[92/06/15            pds]
 * 	64bit cleanup.
 * 	[92/12/02            af]
 *
 * Revision 2.12  92/05/22  18:38:36  jfriedl
 * 	From Mike Kupfer <kupfer@sprite.Berkeley.EDU>:
 * 	Add declaration for cthread_wire().
 * 	Merge in Jonathan Chew's changes for thread-local data.
 * 	Use MACRO_BEGIN and MACRO_END.
 *
 * Revision 1.8  91/03/25  14:14:49  jjc
 * 	For compatibility with cthread_data:
 * 		1) Added private_data field to cthread structure
 * 		   for use by POSIX thread specific data routines.
 * 		2) Conditionalized old data field used by cthread_data
 * 		   under CTHREAD_DATA for binary compatibility.
 * 		3) Changed macros, cthread_set_data and cthread_data,
 * 		   into routines which use the POSIX routines for
 * 		   source compatibility.
 * 		   Also, conditionalized under CTHREAD_DATA.
 * 	[91/03/18            jjc]
 * 	Added support for multiplexing the thread specific global
 * 	variable, cthread_data, using the POSIX threads interface
 * 	for thread private data.
 * 	[91/03/14            jjc]
 *
 * Revision 2.11  91/08/03  18:20:15  jsb
 * 	Removed the infamous line 122.
 * 	[91/08/01  22:40:24  jsb]
 *
 * Revision 2.10  91/07/31  18:35:42  dbg
 * 	Fix the standard-C conditional: it's __STDC__.
 *
 * 	Allow for macro-redefinition of cthread_sp, spin_try_lock,
 * 	spin_unlock (from machine/cthreads.h).
 * 	[91/07/30  17:34:28  dbg]
 *
 * Revision 2.9  91/05/14  17:56:42  mrt
 * 	Correcting copyright
 *
 * Revision 2.8  91/02/14  14:19:52  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:15  mrt]
 *
 * Revision 2.7  90/11/05  14:37:12  rpd
 * 	Include machine/cthreads.h.  Added spin_lock_t.
 * 	[90/10/31            rwd]
 *
 * Revision 2.6  90/10/12  13:07:24  rpd
 * 	Channge to allow for positive stack growth.
 * 	[90/10/10            rwd]
 *
 * Revision 2.5  90/09/09  14:34:56  rpd
 * 	Remove mutex_special and debug_mutex.
 * 	[90/08/24            rwd]
 *
 * Revision 2.4  90/08/07  14:31:14  rpd
 * 	Removed RCS keyword nonsense.
 *
 * Revision 2.3  90/01/19  14:37:18  rwd
 * 	Add back pointer to cthread structure.
 * 	[90/01/03            rwd]
 * 	Change definition of cthread_init and change ur_cthread_self macro
 * 	to reflect movement of self pointer on stack.
 * 	[89/12/18  19:18:34  rwd]
 *
 * Revision 2.2  89/12/08  19:53:49  rwd
 * 	Change spin_try_lock to int.
 * 	[89/11/30            rwd]
 * 	Changed mutex macros to deal with special mutexs
 * 	[89/11/26            rwd]
 * 	Make mutex_{set,clear}_special routines instead of macros.
 * 	[89/11/25            rwd]
 * 	Added mutex_special to specify a need to context switch on this
 * 	mutex.
 * 	[89/11/21            rwd]
 *
 * 	Made mutex_lock a macro trying to grab the spin_lock first.
 * 	[89/11/13            rwd]
 * 	Removed conditionals.  Mutexes are more like conditions now.
 * 	Changed for limited kernel thread version.
 * 	[89/10/23            rwd]
 *
 * Revision 2.1  89/08/03  17:09:40  rwd
 * Created.
 *
 *
 * 28-Oct-88  Eric Cooper (ecc) at Carnegie Mellon University
 *	Implemented spin_lock() as test and test-and-set logic
 *	(using mutex_try_lock()) in sync.c.  Changed ((char *) 0)
 *	to 0, at Mike Jones's suggestion, and turned on ANSI-style
 *	declarations in either C++ or _STDC_.
 *
 * 29-Sep-88  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed NULL to ((char *) 0) to avoid dependency on <stdio.h>,
 *	at Alessandro Forin's suggestion.
 *
 * 08-Sep-88  Alessandro Forin (af) at Carnegie Mellon University
 *	Changed queue_t to cthread_queue_t and string_t to char *
 *	to avoid conflicts.
 *
 * 01-Apr-88  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed compound statement macros to use the
 *	do { ... } while (0) trick, so that they work
 *	in all statement contexts.
 *
 * 19-Feb-88  Eric Cooper (ecc) at Carnegie Mellon University
 *	Made spin_unlock() and mutex_unlock() into procedure calls
 *	rather than macros, so that even smart compilers can't reorder
 *	the clearing of the lock.  Suggested by Jeff Eppinger.
 *	Removed the now empty <machine>/cthreads.h.
 *
 * 01-Dec-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed cthread_self() to mask the current SP to find
 *	the self pointer stored at the base of the stack.
 *
 * 22-Jul-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Fixed bugs in mutex_set_name and condition_set_name
 *	due to bad choice of macro formal parameter name.
 *
 * 21-Jul-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Moved #include <machine/cthreads.h> to avoid referring
 *	to types before they are declared (required by C++).
 *
 *  9-Jul-87  Michael Jones (mbj) at Carnegie Mellon University
 *	Added conditional type declarations for C++.
 *	Added _cthread_init_routine and _cthread_exit_routine variables
 *	for automatic initialization and finalization by crt0.
 */
/*
 * 	File: 	cthreads.h
 *	Author: Eric Cooper, Carnegie Mellon University
 *	Date:	Jul, 1987
 *
 * 	Definitions for the C Threads package.
 *
 */


#ifndef	_CTHREADS_
#define	_CTHREADS_ 1

#if 0
/* This is CMU's machine-dependent file.  In GNU all of the machine
   dependencies are dealt with in libc.  */
#include <machine/cthreads.h>
#include <mach.h>
#include <sys/macro_help.h>
#include <mach/machine/vm_param.h>

#ifdef __STDC__
extern void *malloc();
#else
extern char *malloc();
#endif

#else  /* GNU */
# include <stdlib.h>
# include <mach.h>
# include <mach/machine/vm_param.h>
# include <machine-sp.h>
# define cthread_sp()	((vm_address_t) __thread_stack_pointer ())
# define MACRO_BEGIN	__extension__ ({
# define MACRO_END	0; })
#endif

typedef void *any_t;	    /* XXX - obsolete, should be deleted. */

#if	defined(TRUE)
#else	/* not defined(TRUE) */
#define	TRUE	1
#define	FALSE	0
#endif

/* Enable mutex holder debugging */
/* #define WAIT_DEBUG */
/* Record function name instead of thread pointer */
/* #define WAIT_FUNC_DEBUG */

/*
 * C Threads package initialization.
 */

extern vm_offset_t cthread_init(void);


/*
 * Queues.
 */
typedef struct cthread_queue {
	struct cthread_queue_item *head;
	struct cthread_queue_item *tail;
} *cthread_queue_t;

typedef struct cthread_queue_item {
	struct cthread_queue_item *next;
} *cthread_queue_item_t;

#define	NO_QUEUE_ITEM	((cthread_queue_item_t) 0)

#define	QUEUE_INITIALIZER	{ NO_QUEUE_ITEM, NO_QUEUE_ITEM }

#define	cthread_queue_alloc()	((cthread_queue_t) calloc(1, sizeof(struct cthread_queue)))
#define	cthread_queue_init(q)	((q)->head = (q)->tail = 0)
#define	cthread_queue_free(q)	free((q))

#define	cthread_queue_enq(q, x) \
	MACRO_BEGIN \
		(x)->next = 0; \
		if ((q)->tail == 0) \
			(q)->head = (cthread_queue_item_t) (x); \
		else \
			(q)->tail->next = (cthread_queue_item_t) (x); \
		(q)->tail = (cthread_queue_item_t) (x); \
	MACRO_END

#define	cthread_queue_preq(q, x) \
	MACRO_BEGIN \
		if ((q)->tail == 0) \
			(q)->tail = (cthread_queue_item_t) (x); \
		((cthread_queue_item_t) (x))->next = (q)->head; \
		(q)->head = (cthread_queue_item_t) (x); \
	MACRO_END

#define	cthread_queue_head(q, t)	((t) ((q)->head))

#define	cthread_queue_deq(q, t, x) \
	MACRO_BEGIN \
	if (((x) = (t) ((q)->head)) != 0 && \
	    ((q)->head = (cthread_queue_item_t) ((x)->next)) == 0) \
		(q)->tail = 0; \
	MACRO_END

#define	cthread_queue_map(q, t, f) \
	MACRO_BEGIN \
		register cthread_queue_item_t x, next; \
		for (x = (cthread_queue_item_t) ((q)->head); x != 0; x = next){\
			next = x->next; \
			(*(f))((t) x); \
		} \
	MACRO_END

#if 1

/* In GNU, spin locks are implemented in libc.
   Just include its header file.  */
#include <spin-lock.h>

#else  /* Unused CMU code.  */

/*
 * Spin locks.
 */
extern void		spin_lock_solid(spin_lock_t *_lock);

#if	defined(spin_unlock)
#else	/* not defined(spin_unlock) */
extern void		spin_unlock(spin_lock_t *_lock);
#endif

#if	defined(spin_try_lock)
#else	/* not defined(spin_try_lock) */
extern boolean_t	spin_try_lock(spin_lock_t *_lock);
#endif

#define spin_lock(p) \
	MACRO_BEGIN \
	if (!spin_try_lock(p)) { \
		spin_lock_solid(p); \
	} \
	MACRO_END

#endif /* End unused CMU code.  */

/*
 * Mutex objects.
 */
typedef struct mutex {
  /* The `held' member must be first in GNU.  The GNU C library relies on
     being able to cast a `struct mutex *' to a `spin_lock_t *' (which is
     kosher if it is the first member) and spin_try_lock that address to
     see if it gets the mutex.  */
	spin_lock_t held;
	spin_lock_t lock;
	const char *name;
	struct cthread_queue queue;
	/* holder is for WAIT_DEBUG. Not ifdeffed to keep size constant. */
#ifdef WAIT_FUNC_DEBUG
	const char *fname;
#else /* WAIT_FUNC_DEBUG */
	struct cthread *holder;
#endif /* WAIT_FUNC_DEBUG */
} *mutex_t;

#ifdef WAIT_DEBUG
#ifdef WAIT_FUNC_DEBUG
#define WAIT_CLEAR_DEBUG(m)	(m)->fname = 0
#define WAIT_SET_DEBUG(m)	(m)->fname = __FUNCTION__
#else /* WAIT_FUNC_DEBUG */
#define WAIT_CLEAR_DEBUG(m)	(m)->holder = 0
#define WAIT_SET_DEBUG(m)	(m)->holder = cthread_self()
#endif /* WAIT_FUNC_DEBUG */
#else /* WAIT_DEBUG */
#define WAIT_CLEAR_DEBUG(m)	(void) 0
#define WAIT_SET_DEBUG(m)	(void) 0
#endif /* WAIT_DEBUG */

/* Rearranged accordingly for GNU: */
#define	MUTEX_INITIALIZER	{ SPIN_LOCK_INITIALIZER, SPIN_LOCK_INITIALIZER, 0, QUEUE_INITIALIZER, }
#define	MUTEX_NAMED_INITIALIZER(Name) { SPIN_LOCK_INITIALIZER, SPIN_LOCK_INITIALIZER, Name, QUEUE_INITIALIZER, }

#define	mutex_alloc()		((mutex_t) calloc(1, sizeof(struct mutex)))
#define	mutex_init(m) \
	MACRO_BEGIN \
	spin_lock_init(&(m)->lock); \
	cthread_queue_init(&(m)->queue); \
	spin_lock_init(&(m)->held); \
	WAIT_CLEAR_DEBUG(m); \
	MACRO_END
#define	mutex_set_name(m, x)	((m)->name = (x))
#define	mutex_name(m)		((m)->name != 0 ? (m)->name : "?")
#define	mutex_clear(m)		mutex_init(m)
#define	mutex_free(m)		free((m))

#define mutex_try_lock(m) (spin_try_lock(&(m)->held) ? WAIT_SET_DEBUG(m), 1 : 0)
#define mutex_lock(m) \
	MACRO_BEGIN \
	if (!spin_try_lock(&(m)->held)) { \
		__mutex_lock_solid(m); \
	} \
	WAIT_SET_DEBUG(m); \
	MACRO_END
#define mutex_unlock(m) \
	MACRO_BEGIN \
	if (spin_unlock(&(m)->held), \
	    cthread_queue_head(&(m)->queue, vm_offset_t) != 0) { \
		__mutex_unlock_solid(m); \
	} \
	WAIT_CLEAR_DEBUG(m); \
	MACRO_END
/*
 * Condition variables.
 */
typedef struct condition {
	spin_lock_t lock;
	struct cthread_queue queue;
	const char *name;
	struct cond_imp *implications;
} *condition_t;

struct cond_imp
{
  struct condition *implicatand;
  struct cond_imp *next;
};

#define	CONDITION_INITIALIZER		{ SPIN_LOCK_INITIALIZER, QUEUE_INITIALIZER, 0, 0 }
#define	CONDITION_NAMED_INITIALIZER(Name)		{ SPIN_LOCK_INITIALIZER, QUEUE_INITIALIZER, Name, 0 }

#define	condition_alloc() \
	((condition_t) calloc(1, sizeof(struct condition)))
#define	condition_init(c) \
	MACRO_BEGIN \
	spin_lock_init(&(c)->lock); \
	cthread_queue_init(&(c)->queue); \
	(c)->name = 0; \
	(c)->implications = 0; \
	MACRO_END
#define	condition_set_name(c, x)	((c)->name = (x))
#define	condition_name(c)		((c)->name != 0 ? (c)->name : "?")
#define	condition_clear(c) \
	MACRO_BEGIN \
	condition_broadcast(c); \
	spin_lock(&(c)->lock); \
	MACRO_END
#define	condition_free(c) \
	MACRO_BEGIN \
	condition_clear(c); \
	free((c)); \
	MACRO_END

#define	condition_signal(c) \
	MACRO_BEGIN \
	if ((c)->queue.head || (c)->implications) { \
		cond_signal(c); \
	} \
	MACRO_END

#define	condition_broadcast(c) \
	MACRO_BEGIN \
	if ((c)->queue.head || (c)->implications) { \
		cond_broadcast(c); \
	} \
	MACRO_END

extern int	cond_signal(condition_t _cond);

extern void	cond_broadcast(condition_t _cond);

extern void	condition_wait(condition_t _cond, mutex_t _mutex);
extern int	hurd_condition_wait(condition_t _cond, mutex_t _mutex);

extern void	condition_implies(condition_t _implicator,
				  condition_t _implicatand);
extern void	condition_unimplies(condition_t _implicator,
				    condition_t _implicatand);

/*
 * Threads.
 */

typedef void *	(*cthread_fn_t)(void *arg);

#include <setjmp.h>

typedef struct cthread {
	struct cthread *next;
	struct mutex lock;
	struct condition done;
	int state;
	jmp_buf catch_exit;
	cthread_fn_t func;
	void *arg;
	void *result;
	const char *name;
	void *data;
	void *ldata;
	void *private_data;
	struct ur_cthread *ur;
} *cthread_t;

#define	NO_CTHREAD	((cthread_t) 0)

extern cthread_t	cthread_fork(cthread_fn_t _func, void *_arg);

extern void		cthread_detach(cthread_t _thread);

extern any_t		cthread_join(cthread_t _thread);

extern void		cthread_yield(void);

extern void		cthread_exit(void *_result);

/*
 * This structure must agree with struct cproc in cthread_internals.h
 */
typedef struct ur_cthread {
	struct ur_cthread *next;
	cthread_t incarnation;
} *ur_cthread_t;

#ifndef	cthread_sp
extern vm_offset_t
cthread_sp(void);
#endif

extern vm_offset_t cthread_stack_mask;

#if	defined(STACK_GROWTH_UP)
#define	ur_cthread_ptr(sp) \
	(* (ur_cthread_t *) ((sp) & cthread_stack_mask))
#else	/* not defined(STACK_GROWTH_UP) */
#define	ur_cthread_ptr(sp) \
	(* (ur_cthread_t *) ( ((sp) | cthread_stack_mask) + 1 \
			      - sizeof(ur_cthread_t *)) )
#endif	/* defined(STACK_GROWTH_UP) */

#define	ur_cthread_self()	(ur_cthread_ptr(cthread_sp()))

#define	cthread_assoc(id, t)	((((ur_cthread_t) (id))->incarnation = (t)), \
				((t) ? ((t)->ur = (ur_cthread_t)(id)) : 0))
#define	cthread_self()		(ur_cthread_self()->incarnation)

extern void		cthread_set_name(cthread_t _thread, const char *_name);

extern const char *	cthread_name(cthread_t _thread);

extern int		cthread_count(void);

extern void		cthread_set_limit(int _limit);

extern int		cthread_limit(void);

extern void		cthread_set_kernel_limit(int _n);

extern int		cthread_kernel_limit(void);

extern void		cthread_wire(void);

extern void		cthread_unwire(void);

extern void		cthread_msg_busy(mach_port_t _port, int _min, int _max);

extern void		cthread_msg_active(mach_port_t _prt, int _min, int _max);

extern mach_msg_return_t cthread_mach_msg(mach_msg_header_t *_header,
					  mach_msg_option_t _option,
					  mach_msg_size_t _send_size,
					  mach_msg_size_t _rcv_size,
					  mach_port_t _rcv_name,
					  mach_msg_timeout_t _timeout,
					  mach_port_t _notify,
					  int _min, int _max);

extern void		cthread_fork_prepare(void);

extern void		cthread_fork_parent(void);

extern void		cthread_fork_child(void);

#if	defined(THREAD_CALLS)
/*
 * Routines to replace thread_*.
 */
extern kern_return_t	cthread_get_state(cthread_t _thread);

extern kern_return_t	cthread_set_state(cthread_t _thread);

extern kern_return_t	cthread_abort(cthread_t _thread);

extern kern_return_t	cthread_resume(cthread_t _thread);

extern kern_return_t	cthread_suspend(cthread_t _thread);

extern kern_return_t	cthread_call_on(cthread_t _thread);
#endif	/* defined(THREAD_CALLS) */

#if	defined(CTHREAD_DATA_XX)
/*
 * Set or get thread specific "global" variable
 *
 * The thread given must be the calling thread (ie. thread_self).
 * XXX This is for compatibility with the old cthread_data. XXX
 */
extern int		cthread_set_data(cthread_t _thread, void *_val);

extern void *		cthread_data(cthread_t _thread);
#else	/* defined(CTHREAD_DATA_XX) */

#define cthread_set_data(_thread, _val) ((_thread)->data) = (void *)(_val);
#define cthread_data(_thread) ((_thread)->data)

#define cthread_set_ldata(_thread, _val) ((_thread)->ldata) = (void *)(_val);
#define cthread_ldata(_thread) ((_thread)->ldata)

#endif	/* defined(CTHREAD_DATA_XX) */


/*
 * Support for POSIX thread specific data
 *
 * Multiplexes a thread specific "global" variable
 * into many thread specific "global" variables.
 */
#define CTHREAD_DATA_VALUE_NULL		(void *)0
#define	CTHREAD_KEY_INVALID		(cthread_key_t)-1

typedef int	cthread_key_t;

/*
 * Create key to private data visible to all threads in task.
 * Different threads may use same key, but the values bound to the key are
 * maintained on a thread specific basis.
 */
extern int		cthread_keycreate(cthread_key_t *_key);

/*
 * Get value currently bound to key for calling thread
 */
extern int		cthread_getspecific(cthread_key_t _key, void **_value);

/*
 * Bind value to given key for calling thread
 */
extern int		cthread_setspecific(cthread_key_t _key, void *_value);

/*
 * Debugging support.
 */
#if	defined(DEBUG)

#if	defined(ASSERT)
#else	/* not defined(ASSERT) */
/*
 * Assertion macro, similar to <assert.h>
 */
#include <stdio.h>
#define	ASSERT(p) \
	MACRO_BEGIN \
	if (!(p)) { \
		fprintf(stderr, \
			"File %s, line %d: assertion p failed.\n", \
			__FILE__, __LINE__); \
		abort(); \
	} \
	MACRO_END

#endif	/* defined(ASSERT) */

#define	SHOULDNT_HAPPEN	0

extern int cthread_debug;

#else	/* not defined(DEBUG) */

#if	defined(ASSERT)
#else	/* not defined(ASSERT) */
#define	ASSERT(p)
#endif	/* defined(ASSERT) */

#endif	/* defined(DEBUG) */

#endif	/* not defined(_CTHREADS_) */
