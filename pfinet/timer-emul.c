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

static void
timer_function (any_t timerp)
{
  struct timer_list *timer = timerp;
  mach_port_t recv;
  error_t err;

  recv = mach_reply_port ();

  timer->thread = mach_thread_self ();

  err = mach_msg (NULL, MACH_RCV_MSG|MACH_RCV_TIMEOUT|MACH_RCV_INTERRUPT, 
		  0, 0, recv, timer->expires * (1000 / HZ), MACH_PORT_NULL);

  timer->thread = MACH_PORT_NULL;

  mach_port_destroy (mach_task_self (), recv);

  if (!err)
    {
      begin_interrupt ();
      (*timer->function)(timer->data);
      end_interrupt ();
    }
}


void
add_timer (struct timer_list *timer)
{
  timer->thread = -1;
  cthread_detach (cthread_fork ((cthread_fn_t) timer_function, timer));
}

int
del_timer (struct timer_list *timer)
{
  thread_t thread;

  
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

  /* Test again, because it might have run and completed the mach_msg after
     we tested above and before we suspended, and we don't want to abort
     the mach_port_destroy and certainly not anything inside the timer function\
     which might have started running. */
  if (timer->thread)
    thread_abort (thread);

  thread_resume (thread);

  /* What to return? */
  return 0; /* ???*/
}

void
init_timer (struct timer_list *timer)
{
  timer->thread = MACH_PORT_NULL;
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
