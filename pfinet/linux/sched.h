#ifndef _HACK_SCHED_H
#define _HACK_SCHED_H

#include <linux/wait.h>
#include <sys/signal.h>
#include <hurd/hurd_types.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <sys/time.h>
#include "mapped-time.h"
#include <assert.h>
#include <mach.h>
#include <asm/system.h>

#define jiffies (fetch_jiffies ())
extern struct task_struct *current;
extern struct task_struct current_contents;

struct task_struct
{
  uid_t pgrp, pid;
  int flags;
  int timeout;
  int signal;
  int blocked;
  int state;
  int isroot;
};

/* FLAGS in task_struct's. */
#define PF_EXITING 1
/* STATE in task_struct's. */
#define TASK_INTERRUPTIBLE 1
#define TASK_RUNNING 2

extern inline int
suser ()
{
  return current->isroot;
};

void wake_up_interruptible (struct wait_queue **);
void interruptible_sleep_on (struct wait_queue **);

void select_wait (struct wait_queue **, select_table *);

void schedule (void);

#define SEL_IN SELECT_READ
#define SEL_OUT SELECT_WRITE
#define SEL_EX SELECT_URG

/* This function is used only to send SIGPIPE to the current
   task.  In all such cases, EPIPE is returned anyhow.  In the
   Hurd, servers are not responsible for SIGPIPE; the library
   does that itself upon receiving EPIPE.  So we can just
   NOP such calls.  */
extern inline int
send_sig (u_long signo, struct task_struct *task, int priv)
{
  assert (signo == SIGPIPE);
  assert (task == current);
  return 0;
}

int fetch_current_time (void);
struct timeval fetch_xtime (void);

#define xtime (fetch_xtime ())
#define CURRENT_TIME (xtime.tv_sec)

static struct timeval _xtime_buf;

extern inline struct timeval
fetch_xtime ()
{
  maptime_read (mapped_time, &_xtime_buf);
  return _xtime_buf;
}

#endif
