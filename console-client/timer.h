/* timer.h - Interface to a timer module for Mach.
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

#ifndef _TIMER_H_
#define _TIMER_H_

#include <errno.h>
#include <maptime.h>


/* Initialize the timer component.  Must be called once at startup.  */
error_t timer_init (void);

/* The data structure of a timer.  A user can set the values EXPIRES,
   DATA and FUNCTION, and should leave the other fields alone.  */
struct timer_list
{
  struct timer_list *next, **prev; /* things like to test "T->prev != NULL" */
  long long expires;

  /* The function to be called when the timer expires.  If the
     function returns a non-zero value, the timer is put back on the
     list.  */
  int (*fnc) (void *);
  void *fnc_data;
};

/* Initialize the timer TIMER.  */
void timer_clear (struct timer_list *timer);

/* Add the timer TIMER to the list.  */
void timer_add (struct timer_list *timer);

/* Remove the timer TIMER from the list.  */
int timer_remove (struct timer_list *timer);

/* Change the expiration time of the timer TIMER to EXPIRES.  */
void timer_change (struct timer_list *timer, long long expires);

static inline long long
fetch_jiffies ()
{
  extern volatile struct mapped_time_value *timer_mapped_time;
  extern long long timer_root_jiffies;
  struct timeval tv;
  long long j;

  maptime_read (timer_mapped_time, &tv);

#define HZ 100
  j = (long long) tv.tv_sec * HZ + ((long long) tv.tv_usec * HZ) / 1000000;
  return j - timer_root_jiffies;
}

#endif	/* _TIMER_H_ */
