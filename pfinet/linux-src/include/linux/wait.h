#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002

#define __WCLONE	0x80000000

#ifdef __KERNEL__

#include <asm/page.h>

struct wait_queue {
	struct task_struct * task;
	struct wait_queue * next;
};

#define WAIT_QUEUE_HEAD(x) ((struct wait_queue *)((x)-1))

static inline void init_waitqueue(struct wait_queue **q)
{
	*q = WAIT_QUEUE_HEAD(q);
}

static inline int waitqueue_active(struct wait_queue **q)
{
	struct wait_queue *head = *q;
	return head && head != WAIT_QUEUE_HEAD(q);
}

#endif /* __KERNEL__ */

#endif
