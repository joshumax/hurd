#ifndef _HACK_SCHED_H
#define _HACK_SCHED_H

#include <linux/wait.h>
#include <sys/signal.h>

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

int send_sig (u_long, struct task_struct *, int);

int fetch_current_time (void);

#define CURRENT_TIME (fetch_current_time())

#endif
