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
#include "pfinet.h"

struct mutex global_lock = MUTEX_INITIALIZER;

struct task_struct current_contents;
struct task_struct *current = &current_contents;

void
interruptible_sleep_on (struct wait_queue **p)
{
  int cancel;
  
  cancel = hurd_condition_wait (&(*p)->c, &global_lock);
  if (cancel)
    current->signal = 1;
}

void
wake_up_interruptible (struct wait_queue **p)
{
  /* tcp.c uses an unitialized wait queue; don't bomb
     if we see it. */
  if (*p)
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


/* Set the contents of current appropriately for an RPC being undertaken
   by USER. */
void
become_task (struct sock_user *user)
{
  /* These fields are not really used currently. */
  current->pgrp = current->pid = 0;
  
  current->flags = 0;
  current->timeout = 0;
  current->signal = current->blocked = 0;
  current->state = TASK_RUNNING;
  current->isroot = user->isroot;
}

void
become_task_protid (struct trivfs_protid *protid)
{
  current->pgrp = current->pid = 0;
  current->flags = 0;
  current->timeout = 0;
  current->signal = current->blocked = 0;
  current->state = TASK_RUNNING;
  current->isroot = protid->isroot;
}
