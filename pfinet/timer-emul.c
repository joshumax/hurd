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

static void
timer_function ((any_t) timerp)
{
  struct timer_list *timer = timerp;
  
  msleep (timerp->expires);
  
  begin_interrupt ();
  (*timer->function)(timer->data);
  end_interrupt ();
  timer->thread = 0;
}


void
add_timer (struct timer_list *timer)
{
  cthread_t thread;
  
  timer->thread = 0;
  thread = cthread_fork (timer_function, timer);
  timer->thread = thread;
  cthread_detach (thread);
}

void
del_timer (struct timer_list *timer)
{
  if (timer->thread)
    {
      int flags;
      
      save_flags (flags);
      cli ();
      cthread_kill (timer->thread);
      restore_flags (flags);
    }
}

void
init_timer (struct timer_list *timer)
{
  timer->thread = 0;
}

  
