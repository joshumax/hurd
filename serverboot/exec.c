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
 * i386-specific routines for loading a.out files.
 */

#include <mach.h>
#include <mach/machine/vm_param.h>
#include "mach-exec.h"

#include <file_io.h>

#ifdef i386

#include <mach/machine/eflags.h>

/*
 *	Machine-dependent portions of execve() for the i386.
 */

#define	STACK_SIZE	(64*1024)

char *set_regs(
	mach_port_t	user_task,
	mach_port_t	user_thread,
	struct exec_info *info,
	int		arg_size)
{
	vm_offset_t	stack_start;
	vm_offset_t	stack_end;
	struct i386_thread_state	regs;
	unsigned int		reg_size;

	/*
	 * Add space for 5 ints to arguments, for
	 * PS program. XXX
	 */
	arg_size += 5 * sizeof(int);

	/*
	 * Allocate stack.
	 */
	stack_end = VM_MAX_ADDRESS;
	stack_start = VM_MAX_ADDRESS - STACK_SIZE;
	(void)vm_allocate(user_task,
			  &stack_start,
			  (vm_size_t)(stack_end - stack_start),
			  FALSE);

	reg_size = i386_THREAD_STATE_COUNT;
	(void)thread_get_state(user_thread,
				i386_THREAD_STATE,
				(thread_state_t)&regs,
				&reg_size);

	regs.eip = info->entry;
	regs.uesp = (int)((stack_end - arg_size) & ~(sizeof(int)-1));

        /* regs.efl |=  EFL_TF;  trace flag*/

	(void)thread_set_state(user_thread,
				i386_THREAD_STATE,
				(thread_state_t)&regs,
				reg_size);

	return (char *)regs.uesp;
}

#elif defined __alpha__


/*
 *	Object:
 *		set_regs			EXPORTED function
 *
 *	Initialize enough state for a thread to run, including
 *	stack memory and stack pointer, and program counter.
 *
 */
#define STACK_SIZE (vm_size_t)(128*1024)

char *set_regs(
	mach_port_t	user_task,
	mach_port_t	user_thread,
	struct exec_info *info,
	int		arg_size)
{
	vm_offset_t	stack_start;
	vm_offset_t	stack_end;
	struct alpha_thread_state	regs;

	natural_t	reg_size;

	/*
	 * Allocate stack.
	 */
	stack_end = VM_MAX_ADDRESS;
	stack_start = stack_end - STACK_SIZE;
	(void)vm_allocate(user_task,
			  &stack_start,
			  (vm_size_t)(STACK_SIZE),
			  FALSE);

	reg_size = ALPHA_THREAD_STATE_COUNT;
	(void)thread_get_state(user_thread,
				ALPHA_THREAD_STATE,
				(thread_state_t)&regs,
				&reg_size);

	regs.pc = info->entry;
	regs.r29 = info->init_dp;
	regs.r30 = (integer_t)((stack_end - arg_size) & ~(sizeof(integer_t)-1));

	(void)thread_set_state(user_thread,
				ALPHA_THREAD_STATE,
				(thread_state_t)&regs,
				reg_size);

	return (char *)regs.r30;
}

#else
# error "Not ported to this architecture!"
#endif
