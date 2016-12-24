/*
 * Mach Operating System
 * Copyright (c) 1993-1987 Carnegie Mellon University
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
 *	File:	kern/kalloc.c
 *	Author:	Avadis Tevanian, Jr.
 *	Date:	1985
 *
 *	General kernel memory allocator.  This allocator is designed
 *	to be used by the kernel to manage dynamic memory fast.
 */

#include <mach.h>
#include <pthread.h>		/* for spin locks */
#include <malloc.h> 		/* for malloc_hook/free_hook */

#include "wiring.h"

static void init_hook (void);
static void *malloc_hook (size_t size, const void *caller);
static void *realloc_hook (void *ptr, size_t size, const void *caller);
static void *memalign_hook (size_t alignment, size_t size, const void *caller);
static void free_hook (void *ptr, const void *caller);

/* GNU libc 2.14 defines this macro to declare hook variables as volatile.
   Define it as empty for older libc versions.  */
#ifndef __MALLOC_HOOK_VOLATILE
# define __MALLOC_HOOK_VOLATILE
#endif

void (*__MALLOC_HOOK_VOLATILE __malloc_initialize_hook) (void) = init_hook;


/* #define	DEBUG */

/*
 *	All allocations of size less than kalloc_max are rounded to the
 *	next highest power of 2.
 */
vm_size_t	kalloc_max;		/* max before we use vm_allocate */
#define		MINSIZE	sizeof(vm_offset_t)		/* minimum allocation size */

struct free_list {
	pthread_spinlock_t	lock;
	vm_offset_t		head;	/* head of free list */
#ifdef	DEBUG
	int			count;
#endif	/*DEBUG*/
};

#define	KLIST_MAX	13
					/* sizes: 4, 8, 16, 32, 64,
						128, 256, 512, 1024,
						2048, 4096, 8192, 16384 */
struct free_list	kfree_list[KLIST_MAX];

pthread_spinlock_t	kget_space_lock;
vm_offset_t		kalloc_next_space = 0;
vm_offset_t		kalloc_end_of_space = 0;

vm_size_t		kalloc_wasted_space = 0;

boolean_t		kalloc_initialized = FALSE;

/*
 *	Initialize the memory allocator.  This should be called only
 *	once on a system wide basis (i.e. first processor to get here
 *	does the initialization).
 *
 *	This initializes all of the zones.
 */

void kalloc_init(void)
{
	int i;

	/*
	 * Support free lists for items up to vm_page_size or
	 * 16Kbytes, whichever is less.
	 */

	if (vm_page_size > (MINSIZE << (KLIST_MAX-1)))
		kalloc_max = (MINSIZE << (KLIST_MAX-1));
	else
		kalloc_max = vm_page_size;

	for (i = 0; i < KLIST_MAX; i++) {
	    pthread_spin_init(&kfree_list[i].lock, PTHREAD_PROCESS_PRIVATE);
	    kfree_list[i].head = 0;
	}
	pthread_spin_init(&kget_space_lock, PTHREAD_PROCESS_PRIVATE);

	/*
	 * Do not allocate memory at address 0.
	 */
	kalloc_next_space = vm_page_size;
	kalloc_end_of_space = vm_page_size;
}

/*
 * Contiguous space allocator for items of less than a page size.
 */
vm_offset_t kget_space(vm_offset_t size)
{
	vm_size_t	space_to_add = 0;
	vm_offset_t	new_space = 0;
	vm_offset_t	addr;

	pthread_spin_lock(&kget_space_lock);
	while (kalloc_next_space + size > kalloc_end_of_space) {
	    /*
	     * Add at least one page to allocation area.
	     */
	    space_to_add = round_page(size);

	    if (new_space == 0) {
		/*
		 * Unlock and allocate memory.
		 * Try to make it contiguous with the last
		 * allocation area.
		 */
		pthread_spin_unlock(&kget_space_lock);

		new_space = kalloc_end_of_space;
		if (vm_map(mach_task_self(),
			   &new_space, space_to_add, (vm_offset_t) 0, TRUE,
			   MEMORY_OBJECT_NULL, (vm_offset_t) 0, FALSE,
			   VM_PROT_DEFAULT, VM_PROT_ALL, VM_INHERIT_DEFAULT)
			!= KERN_SUCCESS)
		    return 0;
		pthread_spin_lock(&kget_space_lock);
		continue;
	    }

	    /*
	     * Memory was allocated in a previous iteration.
	     * Check whether the new region is contiguous with the
	     * old one.
	     */
	    if (new_space != kalloc_end_of_space) {
		/*
		 * Throw away the remainder of the old space,
		 * and start a new one.
		 */
		kalloc_wasted_space +=
			kalloc_end_of_space - kalloc_next_space;
		kalloc_next_space = new_space;
	    }
	    kalloc_end_of_space = new_space + space_to_add;

	    new_space = 0;
	}

	addr = kalloc_next_space;
	kalloc_next_space += size;
	pthread_spin_unlock(&kget_space_lock);

	if (new_space != 0)
	    (void) vm_deallocate(mach_task_self(), new_space, space_to_add);

	return addr;
}

void *kalloc(vm_size_t size)
{
	vm_size_t allocsize;
	vm_offset_t addr;
	struct free_list *fl;

	if (!kalloc_initialized) {
	    kalloc_init();
	    kalloc_initialized = TRUE;
	}

	/* compute the size of the block that we will actually allocate */

	allocsize = size;
	if (size <= kalloc_max) {
	    allocsize = MINSIZE;
	    fl = kfree_list;
	    while (allocsize < size) {
		allocsize <<= 1;
		fl++;
	    }
	}

	/*
	 * If our size is still small enough, check the queue for that size
	 * and allocate.
	 */

	if (allocsize <= kalloc_max) {
	    pthread_spin_lock(&fl->lock);
	    if ((addr = fl->head) != 0) {
		fl->head = *(vm_offset_t *)addr;
#ifdef	DEBUG
		fl->count--;
#endif
		pthread_spin_unlock(&fl->lock);
	    }
	    else {
		pthread_spin_unlock(&fl->lock);
		addr = kget_space(allocsize);
	    }
	}
	else {
	    if (vm_allocate(mach_task_self(), &addr, allocsize, TRUE)
			!= KERN_SUCCESS)
		addr = 0;
	}
	return (void *) addr;
}

void
kfree(	void *data,
	vm_size_t size)
{
	vm_size_t freesize;
	struct free_list *fl;

	freesize = size;
	if (size <= kalloc_max) {
	    freesize = MINSIZE;
	    fl = kfree_list;
	    while (freesize < size) {
		freesize <<= 1;
		fl++;
	    }
	}

	if (freesize <= kalloc_max) {
	    pthread_spin_lock(&fl->lock);
	    *(vm_offset_t *)data = fl->head;
	    fl->head = (vm_offset_t) data;
#ifdef	DEBUG
	    fl->count++;
#endif
	    pthread_spin_unlock(&fl->lock);
	}
	else {
	    (void) vm_deallocate(mach_task_self(), (vm_offset_t)data, freesize);
	}
}

static void
init_hook (void)
{
  __malloc_hook = malloc_hook;
  __realloc_hook = realloc_hook;
  __memalign_hook = memalign_hook;
  __free_hook = free_hook;
}

static void *
malloc_hook (size_t size, const void *caller)
{
  return (void *) kalloc ((vm_size_t) size);
}

static void *
realloc_hook (void *ptr, size_t size, const void *caller)
{
  panic("realloc_hook not implemented");
}

static void *
memalign_hook (size_t alignment, size_t size, const void *caller)
{
  if (alignment > vm_page_size)
    panic("memalign_hook not implemented");
  return malloc_hook(size, caller);
}

static void
free_hook (void *ptr, const void *caller)
{
  /* Just ignore harmless attempts at cleanliness.  */
  /*	panic("free not implemented"); */
}

void malloc_fork_prepare()
{
}

void malloc_fork_parent()
{
}

void malloc_fork_child()
{
}
