#ifndef _HACK_ASM_SYSTEM_H_
#define _HACK_ASM_SYSTEM_H_

#include <cthreads.h>
#include <sys/types.h>

#define intr_count (_fetch_intr_count ())

/* This lock is held when "interrupts" are disabled. */
extern struct mutex global_interrupt_lock;

/* Save the "processor state" in the longword FLAGS. */
/* We define 1 to mean that global_interrupt_lock is held. */

#define save_flags(x) _real_save_flags (&x)
extern inline void 
_real_save_flags (u_long *flagsword)
{
  int locked;
  
  locked = !mutex_try_lock (&global_interrupt_lock);
  if (!locked)
    mutex_unlock (&global_interrupt_lock);
  *flagsword = locked;
}

/* Restore state saved in FLAGS. */
extern inline void
restore_flags (u_long flags)
{
  if (flags)
    mutex_try_lock (&global_interrupt_lock);
  else
    mutex_unlock (&global_interrupt_lock);
}

/* Prevent "interrupts" from happening. */
extern inline void 
cli ()
{
  mutex_try_lock (&global_interrupt_lock);
}

/* Permit "interrupts". */ 
extern inline void
sti ()
{
  mutex_unlock (&global_interrupt_lock);
}

/* In threads set aside to be interrupt threads, they call this
   before doing any real work, thus putting us into "interrupt"
   mode. */
extern inline void
begin_interrupt ()
{
  mutex_lock (&global_interrupt_lock);
  /* Should we suspend the current "user thread"? */
}

/* And then this, at the end of the real work. */
extern inline void
end_interrupt ()
{
  mutex_unlock (&global_interrupt_lock);
  /* Likewise a resumption? */
}

/* Return one if we are in interrupt code. */
extern inline 
_fetch_intr_count ()
{
  u_long locked;
 
  _real_save_flags (&locked);
  return locked;
}

#endif
