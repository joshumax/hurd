#ifndef _HACK_SCHED_H
#define _HACK_SCHED_H

#include <sys/signal.h>
#include <sys/time.h>
#include <mach.h>
#include <hurd/hurd_types.h>
#include <limits.h>
#include <assert-backtrace.h>
#include <pthread.h>
#include <errno.h>

#include "mapped-time.h"

#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/tasks.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/times.h>
#include <linux/timex.h>

#include <asm/system.h>
#if 0
#include <asm/semaphore.h>
#include <asm/page.h>

#include <linux/smp.h>
#include <linux/tty.h>
#include <linux/sem.h>
#include <linux/signal.h>
#include <linux/securebits.h>
#endif

#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/wait.h>
#include <linux/timer.h>


#define jiffies (fetch_jiffies ())

#define current	(&current_contents)
extern struct task_struct current_contents;
struct task_struct
{
  uid_t pgrp, pid;
  int flags;
  int timeout;
  int signal;
  int blocked;
  int state, policy;
  int isroot;
  char *comm;
  struct wait_queue **next_wait;
};

static inline void
prepare_current (int isroot)
{
  current->signal = 0;
  current->isroot = isroot;
  /* All other members are constant zero and ignored.  */
}
#define become_task(user)		prepare_current ((user)->isroot)
#define become_task_protid(protid)	prepare_current ((protid)->isroot)

#define signal_pending(current)	((current)->signal)

/* FLAGS in task_struct's. */
#define PF_EXITING 1
/* STATE in task_struct's. */
#define TASK_INTERRUPTIBLE 0
#define TASK_RUNNING 0
/* policy in task_struct's. */
#define SCHED_YIELD 0

struct semaphore { };


static inline int
suser ()
{
  return current->isroot;
};

static inline int
capable(int cap)
{
  return current->isroot;
}


extern pthread_mutex_t global_lock;

static inline int
interruptible_sleep_on_timeout (struct wait_queue **p, struct timespec *tsp)
{
  pthread_cond_t **condp = (void *) p, *c;
  int isroot;
  struct wait_queue **next_wait;
  error_t err;

  c = *condp;
  if (c == 0)
    {
      c = malloc (sizeof **condp);
      assert_backtrace (c);
      pthread_cond_init (c, NULL);
      *condp = c;
    }

  isroot = current->isroot;	/* This is our context that needs switched.  */
  next_wait = current->next_wait; /* This too, for multiple schedule calls.  */
  current->next_wait = 0;
  err = pthread_hurd_cond_timedwait_np(c, &global_lock, tsp);
  if (err == EINTR)
    current->signal = 1;	/* We got cancelled, mark it for later.  */
  current->isroot = isroot;	/* Switch back to our context.  */
  current->next_wait = next_wait;
  return (err == ETIMEDOUT);
}

static inline void
wake_up_interruptible (struct wait_queue **p)
{
  pthread_cond_t **condp = (void *) p, *c = *condp;
  if (c)
    pthread_cond_broadcast (c);
}
#define wake_up		wake_up_interruptible


static inline void
add_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
  assert_backtrace (current->next_wait == 0);
  current->next_wait = p;
}

static inline void
remove_wait_queue(struct wait_queue ** p, struct wait_queue * wait)
{
  assert_backtrace (current->next_wait == p);
  current->next_wait = 0;
}

static inline void
schedule (void)
{
  assert_backtrace (current->next_wait);
  interruptible_sleep_on_timeout (current->next_wait, NULL);
}

static inline void
process_schedule_timeout (unsigned long data)
{
  struct wait_queue **sleep = (struct wait_queue **) data;

  wake_up_interruptible (sleep);
}

static inline long
schedule_timeout (long timeout)
{
  long expire = timeout + jiffies;
  struct timer_list timer;
  static struct wait_queue *sleep = 0;  /* See comment in wait.h why this suffices.  */
  /* TODO: but free it !! */

  init_timer (&timer);
  timer.expires = expire;
  timer.data = (unsigned long) &sleep;
  timer.function = process_schedule_timeout;
  add_timer (&timer);

  interruptible_sleep_on_timeout (&sleep, NULL);
  if (signal_pending (current))
    {
      /* We were canceled.  */
      del_timer (&timer);
      expire -= jiffies;
      if (expire >= 0)
	return expire;
      else
	return 0;
    }
  /* It may happen that we get woken without a signal. Noticed notably during
     rsyslog testsuite.  Make sure we don't leave our timer in.  */
  del_timer(&timer);
  return 0;
}

#define	MAX_SCHEDULE_TIMEOUT	LONG_MAX

/* This function is used only to send SIGPIPE to the current
   task.  In all such cases, EPIPE is returned anyhow.  In the
   Hurd, servers are not responsible for SIGPIPE; the library
   does that itself upon receiving EPIPE.  So we can just
   NOP such calls.  */
static inline int
send_sig (u_long signo, struct task_struct *task, int priv)
{
  assert_backtrace (signo == SIGPIPE);
  assert_backtrace (task == current);
  return 0;
}


#endif
