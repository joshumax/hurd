/*
   Copyright (C) 1995,96,2000,02 Free Software Foundation, Inc.
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

/* Do not include glue-include/linux/errno.h */
#define _HACK_ERRNO_H

#include <linux/timer.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <error.h>
#include <string.h>
#include "pfinet.h"

long long root_jiffies;
volatile struct mapped_time_value *mapped_time;

static struct timer_list *timers;
static thread_t timer_thread = 0;

static void *
timer_function (void *this_is_a_pointless_variable_with_a_rather_long_name)
{
  mach_port_t recv;
  int wait = 0;

  recv = mach_reply_port ();

  timer_thread = mach_thread_self ();

  pthread_mutex_lock (&global_lock);
  while (1)
    {
      int jiff = jiffies;

      if (!timers)
	wait = -1;
      else if (timers->expires <= jiff)
	wait = 0;
      else
	wait = ((timers->expires - jiff) * 1000) / HZ;

      pthread_mutex_unlock (&global_lock);

      mach_msg (NULL, (MACH_RCV_MSG | MACH_RCV_INTERRUPT
		       | (wait == -1 ? 0 : MACH_RCV_TIMEOUT)),
		0, 0, recv, wait, MACH_PORT_NULL);

      pthread_mutex_lock (&global_lock);

      while (timers->expires <= jiffies)
	{
	  struct timer_list *tp;

	  tp = timers;

	  timers = timers->next;
	  if (timers)
	    timers->prev = &timers;

	  tp->next = 0;
	  tp->prev = 0;

	  (*tp->function) (tp->data);
	}
    }

  return NULL;
}


void
add_timer (struct timer_list *timer)
{
  struct timer_list **tp;

  for (tp = &timers; *tp; tp = &(*tp)->next)
    if ((*tp)->expires > timer->expires)
      {
	timer->next = *tp;
	timer->next->prev = &timer->next;
	timer->prev = tp;
	*tp = timer;
	break;
      }
  if (!*tp)
    {
      timer->next = 0;
      timer->prev = tp;
      *tp = timer;
    }

  if (timers == timer)
    {
      /* We have change the first one, so tweak the timer thread
	 to push things up. */
      while (timer_thread == 0)
	swtch_pri (0);

      if (timer_thread != mach_thread_self ())
	{
	  thread_suspend (timer_thread);
	  thread_abort (timer_thread);
	  thread_resume (timer_thread);
	}
    }
}

int
del_timer (struct timer_list *timer)
{
  if (timer->prev)
    {
      *timer->prev = timer->next;
      if (timer->next)
	timer->next->prev = timer->prev;

      timer->next = 0;
      timer->prev = 0;
      return 1;
    }
  else
    return 0;
}

void
mod_timer (struct timer_list *timer, unsigned long expires)
{
  /* Should optimize this.  */
  del_timer (timer);
  timer->expires = expires;
  add_timer (timer);
}


void
init_timer (struct timer_list *timer)
{
  memset (timer, 0, sizeof(struct timer_list));
}

void
init_time ()
{
  error_t err;
  struct timeval tp;
  pthread_t thread;

  err = maptime_map (0, 0, &mapped_time);
  if (err)
    error (2, err, "cannot map time device");

  maptime_read (mapped_time, &tp);

  root_jiffies = (long long) tp.tv_sec * HZ
    + ((long long) tp.tv_usec * HZ) / 1000000;

  err = pthread_create (&thread, NULL, timer_function, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
}
