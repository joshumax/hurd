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

#include <asm/system.h>
#include <linux/sched.h>

struct mutex global_interrupt_lock = MUTEX_INITIALIZER;

struct task_struct *current;

struct mutex user_kernel_lock = MUTEX_INITIALIZER;

/* Call this before doing kernel-level calls; this enforces the
   non-preemptibility of the kernel. */
void
start_kernel (struct task_struct *task)
{
  mutex_lock (&user_kernel_lock);
  mutex_lock (&global_interrupt_lock);
  current = task;
  mutex_unlock (&global_interrupt_lock);
}

/* Call this when done doing a kernel-level call. */
void
end_kernel (void)
{
  mutex_lock (&global_interrupt_lock);
  current = 0;
  mutex_unlock (&global_interrupt_lock);
  mutex_unlock (&user_kernel_lock);
}

void
interruptible_sleep_on (struct wait_queue **p)
{
  condition_wait (&(*p)->c, &user_kernel_lock);
}

void
wake_up_interruptible (struct wait_queue **p)
{
  condition_broadcast (&(*p)->c);
}


/* Wake up the owner of the SOCK.  If HOW is zero, then just
   send SIGIO.  If HOW is one, then send SIGIO only if the 
   SO_WAITDATA flag is off.  If HOW is two, then send SIGIO
   only if the SO_NOSPACE flag is on, and also clear it. */
int
sock_wake_async (struct socket *sock, int how)
{
  /* For now, do nothing. XXX  */
  return 0;
}

/* Record that we are doing a select.  The table P is passed 
   to the protocol-specific select routine and then echoed
   through to us.  The WAIT_ADDRESS is what will be woken up
   when I/O becomes possible.  */
void
select_wait (struct wait_queue **wait_address, select_table *p)
{
  /* For now, do nothing. XXX */
  return;
}

