#ifndef __LINUX_SMPLOCK_H
#define __LINUX_SMPLOCK_H

#ifndef __SMP__

#define lock_kernel()				do { } while(0)
#define unlock_kernel()				do { } while(0)
#define release_kernel_lock(task, cpu)		do { } while(0)
#define reacquire_kernel_lock(task)		do { } while(0)

#else

#include <asm/smplock.h>

#endif /* __SMP__ */

#endif
