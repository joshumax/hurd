#ifndef _HACK_WAIT_H_
#define _HACK_WAIT_H_

#include <pthread.h>

/* This data structure actually represents one waiter on a wait queue,
   and waiters always expect to initialize it with { current, NULL }.
   The actual wait queue is a `struct wait_queue *' stored somewhere.
   We ignore these structures provided by the waiters entirely.
   In the `struct wait_queue *' that is the "head of the wait queue" slot,
   we actually store a `pthread_cond_t *' pointing to malloc'd storage.  */

struct wait_queue
{
  struct task_struct *task;	/* current */
  struct wait_queue *next;	/* NULL */
};


struct select_table_elt
{
  pthread_cond_t *dependent_condition;
  struct select_table_elt *next;
};

typedef struct select_table_struct
{
  pthread_cond_t master_condition;
  struct select_table_elt *head;
} select_table;

#endif
