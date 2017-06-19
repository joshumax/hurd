/* Modified condition_wait that checks for cancellation.
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc., 675 Mass Ave,
   Cambridge, MA 02139, USA.  */

#include <hurd/signal.h>
#include <cthreads.h>
#include "cthread_internals.h"
#include <assert-backtrace.h>

/* Just like condition_wait, but cancellable.  Returns true if cancelled.  */
int
hurd_condition_wait (condition_t c, mutex_t m)
{
  /* This function will be called by hurd_thread_cancel while we are blocked
     in the condition_wait.  We wake up all threads blocked on C,
     so our thread will wake up and notice the cancellation flag.  */
  void cancel_me (void)
    {
      condition_broadcast (c);
    }
  struct hurd_sigstate *ss = _hurd_self_sigstate ();
  cproc_t p = cproc_self ();
  int cancel;

  assert_backtrace (ss->intr_port == MACH_PORT_NULL); /* Sanity check for signal bugs. */

  p->state = CPROC_CONDWAIT | CPROC_SWITCHING;

  /* Atomically enqueue our cproc on the condition variable's queue of
     waiters, and mark our sigstate to indicate that `cancel_me' must be
     called to wake us up.  We must hold the sigstate lock while acquiring
     the condition variable's lock and tweaking it, so that
     hurd_thread_cancel can never suspend us and then deadlock in
     condition_broadcast waiting for the condition variable's lock.  */

  spin_lock (&ss->lock);
  spin_lock (&c->lock);
  cancel = ss->cancel;
  if (cancel)
    /* We were cancelled before doing anything.  Don't block at all.  */
    ss->cancel = 0;
  else
    {
      /* Put us on the queue so that condition_broadcast will know to wake
         us up.  */
      cthread_queue_enq (&c->queue, p);
      /* Tell hurd_thread_cancel how to unblock us.  */
      ss->cancel_hook = &cancel_me;
    }
  spin_unlock (&c->lock);
  spin_unlock (&ss->lock);

  if (cancel)
    {
      /* Cancelled on entry.  Just leave the mutex locked.  */
      m = NULL;
      p->state = CPROC_RUNNING;
    }
  else
    {
      /* Now unlock the mutex and block until woken.  */

#ifdef	WAIT_DEBUG
      p->waiting_for = (char *)c;
#endif	 /* WAIT_DEBUG */

      mutex_unlock (m);

      spin_lock (&p->lock);
      if (p->state & CPROC_SWITCHING)
	cproc_block ();
      else
	{
	  /* We were woken up someplace before reacquiring P->lock.
	     We can just continue on.  */
	  p->state = CPROC_RUNNING;
	  spin_unlock(&p->lock);
	}

#ifdef	WAIT_DEBUG
      p->waiting_for = (char *)0;
#endif	 /* WAIT_DEBUG */
    }

  spin_lock (&ss->lock);
  /* Clear the hook, now that we are done blocking.  */
  ss->cancel_hook = NULL;
  /* Check the cancellation flag; we might have unblocked due to
     cancellation rather than a normal condition_signal or
     condition_broadcast (or we might have just happened to get cancelled
     right after waking up).  */
  cancel |= ss->cancel;
  ss->cancel = 0;
  spin_unlock (&ss->lock);

  if (m)
    /* Reacquire the mutex and return.  */
    mutex_lock (m);

  return cancel;
}
