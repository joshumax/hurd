#ifndef _HACK_SCHED_H
#define _HACK_SCHED_H

#include <linux/wait.h>
#include <sys/signal.h>
#include <hurd/hurd_types.h>

extern unsigned long volatile jiffies;
#define HZ 100
extern struct task_struct *current;

struct task_struct
{
  uid_t pgrp, pid;
  int flags;
  int timeout;
  int signal;
  int blocked;
};

/* FLAGS in task_struct's. */
#define PF_EXITING 1

void wake_up_interruptible (struct wait_queue **);
void interruptible_sleep_on (struct wait_queue **);

void select_wait (struct wait_queue **, select_table *);

#define SEL_IN SELECT_READ
#define SEL_OUT SELECT_WRITE
#define SEL_EX SELECT_URG

int send_sig (u_long, struct task_struct *, int);

int fetch_current_time (void);
struct timeval fetch_xtime (void);

#define CURRENT_TIME (fetch_current_time())
#define xtime (fetch_xtime ())

#endif
