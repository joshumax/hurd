#ifndef _HACK_MM_H_
#define _HACK_MM_H_

#include <linux/kernel.h>
#include <linux/sched.h>

/* All memory addresses are presumptively valid, because they are
   all internal. */
#define verify_area(a,b,c) 0

#define VERIFY_READ 0
#define VERIFY_WRITE 0
#define GFP_ATOMIC 0
#define GFP_KERNEL 0
#define GFP_BUFFER 0
#define __GFP_WAIT 0

#include <mach.h>
#include <sys/mman.h>
#include <stdint.h>
#include <mach/vm_param.h>

#define PAGE_SIZE	(1 << PAGE_SHIFT)

/* The one use of this is by net/ipv4/tcp.c::tcp_init, which
   uses the power of two above `num_physpages >> (20 - PAGE_SHIFT)'
   as a starting point and halves from there the number of pages
   it tries to allocate for the hash table of TCP connections.  */
#define num_physpages	(64 << 20 >> PAGE_SHIFT) /* XXX calculate for 32MB */

static inline uintptr_t
__get_free_pages (int gfp_mask, unsigned long int order)
{
  void *ptr = mmap (0, PAGE_SIZE << order,
		    PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  return ptr == MAP_FAILED ? 0 : (uintptr_t) ptr;
}

#endif
