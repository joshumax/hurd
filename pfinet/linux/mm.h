#ifndef _HACK_MM_H_
#define _HACK_MM_H_

#include <linux/kernel.h>
#include <linux/sched.h>

int verify_area (int, const void *, u_long);

#define VERIFY_READ 0
#define VERIFY_WRITE 0
#define GFP_ATOMIC 0
#define GFP_KERNEL 0

#endif
