#ifndef _HACK_ASM_SPINLOCK_H_
#define _HACK_ASM_SPINLOCK_H_

typedef struct { } spinlock_t;
#define SPIN_LOCK_UNLOCKED { }

#undef	spin_lock_init
#undef	spin_lock
#undef	spin_unlock

#define spin_lock_init(lock)	((void) (lock))
#define spin_lock(lock)		((void) (lock))
#define spin_trylock(lock)	((void) (lock), 1)
#define spin_unlock_wait(lock)	((void) (lock))
#define spin_unlock(lock)	((void) (lock))
#define spin_lock_irq(lock)	((void) (lock))
#define spin_unlock_irq(lock)	((void) (lock))
#define spin_lock_irqsave(lock, flags)		((void) (lock), (void) (flags))
#define spin_unlock_irqrestore(lock, flags)	((void) (lock), (void) (flags))

typedef struct { } rwlock_t;
#define read_lock(rw)		do { } while(0)
#define write_lock(rw)		do { } while(0)
#define write_unlock(rw)	do { } while(0)
#define read_unlock(rw)		do { } while(0)

#if 0
#include <rwlock.h>

typedef struct mutex spinlock_t;

#undef	spin_lock_init
#undef	spin_lock
#undef	spin_unlock

#define SPIN_LOCK_UNLOCKED	MUTEX_INITIALIZER
#define spin_lock_init(lock)	({ __mutex_init (lock); })
#define spin_lock(lock)		({ __mutex_lock (lock); })
#define spin_trylock(lock)	({ __mutex_trylock (lock); })
#define spin_unlock_wait(lock)	({ __mutex_unlock (lock); })
#define spin_unlock(lock)	({ __mutex_unlock (lock); })
#define spin_lock_irq(lock)	({ __mutex_lock (lock); })
#define spin_unlock_irq(lock)	({ __mutex_unlock (lock); })

#define spin_lock_irqsave(lock, flags) \
	do { flags = 0; __mutex_lock (lock); } while (0)
#define spin_unlock_irqrestore(lock, flags)	({ __mutex_unlock (lock); })


typedef struct rwlock rwlock_t;

#define read_lock(rw)		rwlock_reader_lock(rw)
#define write_lock(rw)		rwlock_writer_lock(rw)
#define write_unlock(rw)	rwlock_writer_unlock(rw)
#define read_unlock(rw)		rwlock_reader_unlock(rw)

#endif


#define read_lock_irq(lock)	read_lock(lock)
#define read_unlock_irq(lock)	read_unlock(lock)
#define write_lock_irq(lock)	write_lock(lock)
#define write_unlock_irq(lock)	write_unlock(lock)

#define read_lock_irqsave(lock, flags)	\
	do { (flags) = 0; read_lock(lock); } while (0)
#define read_unlock_irqrestore(lock, flags) read_unlock(lock)
#define write_lock_irqsave(lock, flags)	\
	do { (flags) = 0; write_lock(lock); } while (0)
#define write_unlock_irqrestore(lock, flags) write_unlock(lock)


#endif
