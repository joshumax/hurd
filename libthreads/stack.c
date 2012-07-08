/*
 * Mach Operating System
 * Copyright (c) 1992,1991,1990 Carnegie Mellon University
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
 * $Log: stack.c,v $
 * Revision 1.8  2002/05/27 02:50:41  roland
 * 2002-05-26  Roland McGrath  <roland@frob.com>
 *
 * 	Changes merged from CMU MK83a version:
 * 	* cthreads.h, options.h: Various cleanups.
 * 	* call.c, cthread_data.c, sync.c, mig_support.c, stack.c: Likewise.
 * 	* i386/cthreads.h, i386/thread.c, i386/lock.s: Likewise.
 * 	* cthread_internals.h: Add decls for internal functions.
 * 	(struct cproc): Use vm_offset_t for stack_base and stack_size members.
 * 	Use natural_t for context member.
 * 	* cprocs.c: Use prototypes for all defns.
 * 	* cthreads.c: Likewise.
 * 	(cthread_exit): Cast any_t to integer_t before int.
 *
 * Revision 2.14  93/01/14  18:05:58  danner
 * 	Converted file to ANSI C.
 * 	[92/12/18            pds]
 * 	64bit cleanup.
 * 	[92/12/02            af]
 *
 * Revision 2.13  92/01/14  16:48:54  rpd
 * 	Fixed addr_range_check to deallocate the object port from vm_region.
 * 	[92/01/14            rpd]
 *
 * Revision 2.12  92/01/03  20:37:10  dbg
 * 	Export cthread_stack_size, and use it if non-zero instead of
 * 	probing the stack.  Fix error in deallocating unused initial
 * 	stack (STACK_GROWTH_UP case).
 * 	[91/08/28            dbg]
 *
 * Revision 2.11  91/07/31  18:39:34  dbg
 * 	Fix some bad stack references (stack direction).
 * 	[91/07/30  17:36:50  dbg]
 *
 * Revision 2.10  91/05/14  17:58:49  mrt
 * 	Correcting copyright
 *
 * Revision 2.9  91/02/14  14:21:08  mrt
 * 	Added new Mach copyright
 * 	[91/02/13  12:41:35  mrt]
 *
 * Revision 2.8  90/11/05  18:10:46  rpd
 * 	Added cproc_stack_base.  Add stack_fork_child().
 * 	[90/11/01            rwd]
 *
 * Revision 2.7  90/11/05  14:37:51  rpd
 * 	Fixed addr_range_check for new vm_region semantics.
 * 	[90/11/02            rpd]
 *
 * Revision 2.6  90/10/12  13:07:34  rpd
 * 	Deal with positively growing stacks.
 * 	[90/10/10            rwd]
 * 	Deal with initial user stacks that are not perfectly aligned.
 * 	[90/09/26  11:51:46  rwd]
 *
 * 	Leave extra stack page around in case it is needed before we
 *	switch stacks.
 * 	[90/09/25            rwd]
 *
 * Revision 2.5  90/08/07  14:31:46  rpd
 * 	Removed RCS keyword nonsense.
 *
 * Revision 2.4  90/06/02  15:14:18  rpd
 * 	Moved cthread_sp to machine-dependent files.
 * 	[90/04/24            rpd]
 * 	Converted to new IPC.
 * 	[90/03/20  20:56:35  rpd]
 *
 * Revision 2.3  90/01/19  14:37:34  rwd
 * 	Move self pointer to top of stack
 * 	[89/12/12            rwd]
 *
 * Revision 2.2  89/12/08  19:49:52  rwd
 * 	Back out change from af.
 * 	[89/12/08            rwd]
 *
 * Revision 2.1.1.3  89/12/06  12:54:17  rwd
 * 	Gap fix from af
 * 	[89/12/06            rwd]
 *
 * Revision 2.1.1.2  89/11/21  15:01:40  rwd
 * 	Add RED_ZONE ifdef.
 * 	[89/11/20            rwd]
 *
 * Revision 2.1.1.1  89/10/24  13:00:44  rwd
 * 	Remove conditionals.
 * 	[89/10/23            rwd]
 *
 * Revision 2.1  89/08/03  17:10:05  rwd
 * Created.
 *
 * 18-Jan-89  David Golub (dbg) at Carnegie-Mellon University
 *	Altered for stand-alone use:
 *	use vm_region to probe for the bottom of the initial thread's
 *	stack.
 *
 *
 * 01-Dec-87  Eric Cooper (ecc) at Carnegie Mellon University
 *	Changed cthread stack allocation to use aligned stacks
 *	and store self pointer at base of stack.
 *	Added inline expansion for cthread_sp() function.
 */
/*
 * 	File: 	stack.c
 *	Author:	Eric Cooper, Carnegie Mellon University
 *	Date:	Dec, 1987
 *
 * 	C Thread stack allocation.
 *
 */

#include <cthreads.h>
#include "cthread_internals.h"
#include <hurd/threadvar.h>

#define	BYTES_TO_PAGES(b)	(((b) + vm_page_size - 1) / vm_page_size)

vm_offset_t cthread_stack_mask;
vm_size_t cthread_stack_size;
private vm_address_t next_stack_base;

/*
 * Set up a stack segment for a thread.
 * Segment has a red zone (invalid page)
 * for early detection of stack overflow.
 * The cproc_self pointer is stored at the top.
 *
 *	--------- (high address)
 *	| self	|
 *	|  ...	|
 *	|	|
 *	| stack	|
 *	|	|
 *	|  ...	|
 *	|	|
 *	---------
 *	|	|
 *	|invalid|
 *	|	|
 *	--------- (stack base)
 *	--------- (low address)
 *
 * or the reverse, if the stack grows up.
 */

private void
setup_stack(p, base)
	register cproc_t p;
	register vm_address_t base;
{
	register kern_return_t r;

	p->stack_base = base;
	/*
	 * Stack size is segment size minus size of self pointer
	 */
	p->stack_size = cthread_stack_size;
	/*
	 * Protect red zone.
	 */
#ifdef  STACK_GROWTH_UP
	MACH_CALL(vm_protect(mach_task_self(), base + cthread_stack_size - 2*vm_page_size, vm_page_size, FALSE, VM_PROT_NONE), r);
#else
	MACH_CALL(vm_protect(mach_task_self(), base + vm_page_size, vm_page_size, FALSE, VM_PROT_NONE), r);
#endif
	/*
	 * Store self pointer.
	 */
	*(cproc_t *)&ur_cthread_ptr(base) = p;
}

#if 0				/* GNU */
private vm_offset_t
addr_range_check(vm_offset_t start_addr, vm_offset_t end_addr,
		 vm_prot_t desired_protection)
{
	register vm_offset_t	addr;

	addr = start_addr;
	while (addr < end_addr) {
	    vm_offset_t		r_addr;
	    vm_size_t		r_size;
	    vm_prot_t		r_protection,
				r_max_protection;
	    vm_inherit_t	r_inheritance;
	    boolean_t		r_is_shared;
	    memory_object_name_t	r_object_name;
	    vm_offset_t		r_offset;
	    kern_return_t	kr;

	    r_addr = addr;
	    kr = vm_region(mach_task_self(), &r_addr, &r_size,
			   &r_protection, &r_max_protection, &r_inheritance,
			   &r_is_shared, &r_object_name, &r_offset);
	    if ((kr == KERN_SUCCESS) && MACH_PORT_VALID(r_object_name))
		(void) mach_port_deallocate(mach_task_self(), r_object_name);

	    if ((kr != KERN_SUCCESS) ||
		(r_addr > addr) ||
		((r_protection & desired_protection) != desired_protection))
		return (0);
	    addr = r_addr + r_size;
	}
	return (addr);
}

/*
 * Probe for bottom and top of stack.
 * Assume:
 * 1. stack grows DOWN
 * 2. There is an unallocated region below the stack.
 */
private void
probe_stack(vm_offset_t *stack_bottom, vm_offset_t *stack_top)
{
	/*
	 * Since vm_region returns the region starting at
	 * or ABOVE the given address, we cannot use it
	 * directly to search downwards.  However, we
	 * also want a size that is the closest power of
	 * 2 to the stack size (so we can mask off the stack
	 * address and get the stack base).  So we probe
	 * in increasing powers of 2 until we find a gap
	 * in the stack.
	 */
	vm_offset_t	start_addr, end_addr;
	vm_offset_t	last_start_addr, last_end_addr;
	vm_size_t	stack_size;

	/*
	 * Start with a page
	 */
	start_addr = cthread_sp() & ~(vm_page_size - 1);
	end_addr   = start_addr + vm_page_size;

	stack_size = vm_page_size;

	/*
	 * Increase the tentative stack size, by doubling each
	 * time, until we have exceeded the stack (some of the
	 * range is not valid).
	 */
	do {
	    /*
	     * Save last addresses
	     */
	    last_start_addr = start_addr;
	    last_end_addr   = end_addr;

	    /*
	     * Double the stack size
	     */
	    stack_size <<= 1;
	    start_addr = end_addr - stack_size;

	    /*
	     * Check that the entire range exists and is writable
	     */
	} while ((end_addr = addr_range_check(start_addr, end_addr,
					      VM_PROT_READ|VM_PROT_WRITE)));
	/*
	 * Back off to previous power of 2.
	 */
	*stack_bottom = last_start_addr;
	*stack_top = last_end_addr;
}
#endif

/* For GNU: */
extern unsigned long int __hurd_threadvar_stack_mask;
extern unsigned long int __hurd_threadvar_stack_offset;
extern unsigned int __hurd_threadvar_max;

vm_offset_t
stack_init(cproc_t p)
{
#if 0
	vm_offset_t	stack_bottom,
			stack_top,
			start;
	vm_size_t	size;
	kern_return_t	r;

	/*
	 * Probe for bottom and top of stack, as a power-of-2 size.
	 */
	probe_stack(&stack_bottom, &stack_top);

	/*
	 * Use the stack size found for the Cthread stack size,
	 * if not already specified.
	 */
	if (cthread_stack_size == 0)
	    cthread_stack_size = stack_top - stack_bottom;
#else  /* GNU */
	if (cthread_stack_size == 0)
	  cthread_stack_size = vm_page_size * 16; /* Reasonable default.  */
#endif /* GNU */

#if	defined(STACK_GROWTH_UP)
	cthread_stack_mask = ~(cthread_stack_size - 1);
#else	/* not defined(STACK_GROWTH_UP) */
	cthread_stack_mask = cthread_stack_size - 1;
#endif	/* defined(STACK_GROWTH_UP) */

        /* Set up the variables so GNU can find its per-thread variables.  */
        __hurd_threadvar_stack_mask = ~(cthread_stack_size - 1);
        /* The GNU per-thread variables will be stored just after the
           cthread-self pointer at the base of the stack.  */
#ifdef  STACK_GROWTH_UP
        __hurd_threadvar_stack_offset = sizeof (ur_cthread_t *);
#else
        __hurd_threadvar_stack_offset = (cthread_stack_size -
                                         sizeof (ur_cthread_t *) -
                                         __hurd_threadvar_max * sizeof (long));
#endif

	/*
	 * Guess at first available region for stack.
	 */
	next_stack_base = 0;

	/*
	 * Set up stack for main thread.
	 */
	alloc_stack(p);

#if 0				/* GNU */
	/*
	 * Delete rest of old stack.
	 */

#if	defined(STACK_GROWTH_UP)
	start = (cthread_sp() | (vm_page_size - 1)) + 1 + vm_page_size;
	size = stack_top - start;
#else	/* not defined(STACK_GROWTH_UP) */
	start = stack_bottom;
	size = (cthread_sp() & ~(vm_page_size - 1)) - stack_bottom -
	       vm_page_size;
#endif	/* defined(STACK_GROWTH_UP) */
	MACH_CALL(vm_deallocate(mach_task_self(),start,size),r);
#endif /* GNU */

	/*
	 * Return new stack; it gets passed back to the caller
	 * of cthread_init who must switch to it.
	 */
	return cproc_stack_base(p, sizeof(ur_cthread_t *) +
				/* Account for GNU per-thread variables.  */
				__hurd_threadvar_max * sizeof (long int));
}

/*
 * Allocate a stack segment for a thread.
 * Stacks are never deallocated.
 *
 * The variable next_stack_base is used to align stacks.
 * It may be updated by several threads in parallel,
 * but mutual exclusion is unnecessary: at worst,
 * the vm_allocate will fail and the thread will try again.
 */

void
alloc_stack(cproc_t p)
{
	vm_address_t base = next_stack_base;

	for (base = next_stack_base;
	     vm_allocate(mach_task_self(), &base, cthread_stack_size, FALSE) != KERN_SUCCESS;
	     base += cthread_stack_size)
		;
	next_stack_base = base + cthread_stack_size;
	setup_stack(p, base);
}

vm_offset_t
cproc_stack_base(cproc, offset)
	register cproc_t cproc;
	register int offset;
{
#if	defined(STACK_GROWTH_UP)
	return (cproc->stack_base + offset);
#else	/* not defined(STACK_GROWTH_UP) */
	return (cproc->stack_base + cproc->stack_size - offset);
#endif	/* defined(STACK_GROWTH_UP) */

}

void stack_fork_child()
/*
 * Called in the child after a fork().  Resets stack data structures to
 * coincide with the reality that we now have a single cproc and cthread.
 */
{
    next_stack_base = 0;
}
