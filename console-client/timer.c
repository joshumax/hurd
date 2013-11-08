/* timer.c - A timer module for Mach.
   Copyright (C) 1995,96,2000,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG and Marcus Brinkmann.

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

#include <errno.h>
#include <string.h>
#include <maptime.h>
#include <mach.h>
#include <pthread.h>
#include <stdio.h>

#include "timer.h"

/* The value of fetch_jiffies() at startup.  */
long long timer_root_jiffies;

/* The mapped time.  */
volatile struct mapped_time_value *timer_mapped_time;


/* The timer thread.  */
static thread_t timer_thread;

/* The lock protects the timer list TIMERS.  */
static pthread_mutex_t timer_lock;

/* A list of all active timers.  */
static struct timer_list *timers;


static inline void
timer_add_internal (struct timer_list *timer)
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
}


/* Make the timer thread aware of new timers at the beginning of the
   list.  */
static inline void
kick_timer_thread (void)
{
  /* XXX This is a whacky notion.  */
  while (!timer_thread)
    swtch_pri (0);

  if (timer_thread != mach_thread_self ())
    {
      thread_suspend (timer_thread);
      thread_abort (timer_thread);
      thread_resume (timer_thread);
    }
}

/* The timer thread.  */
static void *
timer_function (void *this_is_a_pointless_variable_with_a_rather_long_name)
{
  mach_port_t recv = mach_reply_port ();
  int wait = 0;

  timer_thread = mach_thread_self ();

  pthread_mutex_lock (&timer_lock);
  while (1)
    {
      int jiff = fetch_jiffies ();

      if (!timers)
	wait = -1;
      else if (timers->expires < jiff)
	wait = 0;
      else
	wait = ((timers->expires - jiff) * 1000) / HZ;

      pthread_mutex_unlock (&timer_lock);
      mach_msg (NULL, (MACH_RCV_MSG | MACH_RCV_INTERRUPT
		       | (wait == -1 ? 0 : MACH_RCV_TIMEOUT)),
		0, 0, recv, wait, MACH_PORT_NULL);
      pthread_mutex_lock (&timer_lock);

      while (timers && timers->expires < fetch_jiffies ())
	{
	  struct timer_list *tp;

	  tp = timers;

	  timers = timers->next;
	  if (timers)
	    timers->prev = &timers;

	  tp->next = 0;
	  tp->prev = 0;

	  if ((*tp->fnc) (tp->fnc_data))
	    timer_add_internal (tp);
	}
    }

  return NULL;
}


/* Initialize the timer component.  Must be called once at startup.  */
error_t
timer_init (void)
{
  error_t err;
  struct timeval tp;
  pthread_t thread;

  pthread_mutex_init (&timer_lock, NULL);

  err = maptime_map (0, 0, &timer_mapped_time);
  if (err)
    return err;

  maptime_read (timer_mapped_time, &tp);

  timer_root_jiffies = (long long) tp.tv_sec * HZ
    + ((long long) tp.tv_usec * HZ) / 1000000;

  err = pthread_create (&thread, NULL, timer_function, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  return 0;
}


/* Initialize the timer TIMER.  */
void
timer_clear (struct timer_list *timer)
{
  memset (timer, 0, sizeof (struct timer_list));
}

/* Add the timer TIMER to the list.  */
void
timer_add (struct timer_list *timer)
{
  pthread_mutex_lock (&timer_lock);
  timer_add_internal (timer);

  if (timers == timer)
    kick_timer_thread ();

  pthread_mutex_unlock (&timer_lock);
}

/* Remove the timer TIMER from the list.  */
int
timer_remove (struct timer_list *timer)
{
  pthread_mutex_lock (&timer_lock);
  if (timer->prev)
    {
      *timer->prev = timer->next;
      if (timer->next)
	timer->next->prev = timer->prev;

      timer->next = 0;
      timer->prev = 0;
      pthread_mutex_unlock (&timer_lock);
      return 1;
    }
  else
    {
      pthread_mutex_unlock (&timer_lock);
      return 0;
    }
}

/* Change the expiration time of the timer TIMER to EXPIRES.  */
void
timer_change (struct timer_list *timer, long long expires)
{
  /* XXX Should optimize this.  */
  timer_remove (timer);
  timer->expires = expires;
  timer_add (timer);
}
