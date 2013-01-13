#ifndef _HACK_POLL_H_
#define _HACK_POLL_H_

#include <hurd/hurd_types.h>

#define	POLLIN		SELECT_READ
#define	POLLRDNORM	SELECT_READ
#define POLLOUT		SELECT_WRITE
#define POLLWRNORM	SELECT_WRITE
#define POLLWRBAND	SELECT_WRITE
#define	POLLPRI		SELECT_URG
#define	POLLERR		0x1000
#define	POLLHUP		SELECT_READ

typedef struct poll_table_struct { } poll_table;

#include <linux/sched.h>

static inline void
poll_wait(struct file * filp, struct wait_queue ** wait_address, poll_table *p)
{
}

#endif
