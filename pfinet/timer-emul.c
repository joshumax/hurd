/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <linux/timer.h>
#include <asm/system.h>
#include <linux/sched.h>
#include "pfinet.h"

long long root_jiffies;
volatile struct mapped_time_value *mapped_time;

static spin_lock_t timer_lock;
struct timer_list *timers;
mach_thread_t timer_thread;

  err = mach_msg (NULL, MACH_RCV_MSG|MACH_RCV_TIMEOUT|MACH_RCV_INTERRUPT, 
		  0, 0, recv, timer->expires * (1000 / HZ), MACH_PORT_NULL);


void
add_timer (struct timer_list *timer)
{
  spin_lock (&timer_lock);

  timer->expires += jiffies;


  spin_unlock (&timer_lock);
}

int
del_timer (struct timer_list *timer)
{
  thread_t thread;

  spin_lock (&timer_lock);
  
 recheck:
  switch (timer->state)
    {
    case TIMER_INACTIVE:
      break;

    case TIMER_STARTING:
      /* Wait until it's had a chance to set its ID. */
      spin_unlock (&timer_lock);
      swtch_pri (0);
      spin_lock (&timer_lock);
      goto recheck;
      
    case TIMER_STARTED:
      /* 
	
  
  if (timer->thread == -1)
    {
      /* It hasn't had a chance to set its ID.  Wait a bit
	 until it does. */
      do
	swtch_pri (0);
      while (timer->thread == -1);
    }
  
  thread = timer->thread;
  if (thread == MACH_PORT_NULL)
    return 0;  /* ??? */

  thread_suspend (thread);

  /* Test again, because it might have run and completed the mach_msg
     after we tested above and before we suspended, and we don't want
     to abort the mach_port_destroy and certainly not anything inside
     the timer function\ which might have started running. */
  if (timer->thread)
    thread_abort (thread);

  thread_resume (thread);

  /* What to return? */
  return 0; /* ???*/
}

void
init_timer (struct timer_list *timer)
{
  timer->state = TIMER_INACTIVE;
}

void
init_root_jiffies ()
{
  struct timeval tp;
  
  fill_timeval (&tp);
  
  root_jiffies = (long long) tp.tv_sec * HZ 
    + (long long) tp.tv_usec * HZ / 1000.0;
}

void
init_mapped_time ()
{
  device_t timedev;
  memory_object_t timeobj;
  
  device_open (master_device, 0, "time", &timedev);
  device_map (timedev, VM_PROT_READ, 0, sizeof (mapped_time_value_t),
	      &timeobj, 0);
  vm_map (mach_task_self (), (vm_address_t *)&mapped_time,
	  sizeof (mapped_time_value_t), 0, 1, timeobj, 0, 0,
	  VM_PROT_READ, VM_PROT_READ, VM_INHERIT_NONE);
  mach_port_deallocate (mach_task_self (), timedev);
  mach_port_deallocate (mach_task_self (), timeobj);
}
