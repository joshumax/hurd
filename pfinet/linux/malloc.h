#ifndef _HACK_MALLOC_H_
#define _HACK_MALLOC_H_

#include <linux/mm.h>

#define kfree_s(a,b) (free (a))
#define kfree(a) (free (a))
#define kmalloc(a,b) (malloc (a))

#endif
