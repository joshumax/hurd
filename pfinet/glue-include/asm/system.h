#ifndef _HACK_ASM_SYSTEM_H
#define _HACK_ASM_SYSTEM_H

/* We don't need atomicity in the Linux code because we serialize all
   entries to it.  */

#include <stdint.h>

#define xchg(ptr, x)							      \
  ({									      \
    __typeof__ (*(ptr)) *_ptr = (ptr), _x = *_ptr;			      \
    *_ptr = (x); _x;							      \
  })

#define mb()	((void) 0)	/* memory barrier */
#define rmb()	mb()
#define wmb()	mb()


#endif
