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

#endif
