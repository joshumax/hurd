#ifndef _HACK_INTERRUPT_H_
#define _HACK_INTERRUPT_H_

#include <linux/netdevice.h>
#include "pfinet.h"

#define in_interrupt()		(0)
#define synchronize_irq()	((void) 0)

#define synchronize_bh()	((void) 0) /* XXX ? */

/* The code that can call these are already entered holding
   global_lock, which locks out the net_bh worker thread.  */
#define start_bh_atomic()	((void) 0)
#define end_bh_atomic()		((void) 0)
/*
extern pthread_mutex_t net_bh_lock;
#define start_bh_atomic()	pthread_mutex_lock (&net_bh_lock)
#define end_bh_atomic()		pthread_mutex_unlock (&net_bh_lock)
*/

/* See sched.c::net_bh_worker comments.  */
extern pthread_cond_t net_bh_wakeup;
extern int net_bh_raised;

#define NET_BH	0xb00bee51

/* The only call to this ever reached is in net/core/dev.c::netif_rx,
   to announce having enqueued a packet on `backlog'.  */
static inline void
mark_bh (int bh)
{
  assert_backtrace (bh == NET_BH);
  net_bh_raised = 1;
  pthread_cond_broadcast (&net_bh_wakeup);
}

void net_bh (void);
static inline void
init_bh (int bh, void (*fn) (void))
{
  assert_backtrace (bh == NET_BH);
  assert_backtrace (fn == &net_bh);
}

#endif
