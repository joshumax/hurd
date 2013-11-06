/*
   Copyright (C) 1995,96,2000,02 Free Software Foundation, Inc.

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

#include "pfinet.h"

#include <asm/system.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t net_bh_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t net_bh_wakeup = PTHREAD_COND_INITIALIZER;
int net_bh_raised = 0;

struct task_struct current_contents; /* zeros are right default values */


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


/* This function is the "net_bh worker thread".
   The packet receiver thread calls net/core/dev.c::netif_rx with a packet;
   netif_rx either drops the packet, or enqueues it and wakes us up
   via mark_bh which is really condition_broadcast on net_bh_wakeup.
   The packet receiver thread holds net_bh_lock while calling netif_rx.
   We wake up and take global_lock, which locks out RPC service threads.
   We then also take net_bh_lock running net_bh.
   Thus, only this thread running net_bh locks out the packet receiver
   thread (which takes only net_bh_lock while calling netif_rx), so packets
   are quickly moved from the Mach port's message queue to the `backlog'
   queue, or dropped, without synchronizing with RPC service threads.
   (The RPC service threads lock out the running of net_bh, but not
   the queuing/dropping of packets in netif_rx.)  */
void *
net_bh_worker (void *arg)
{
  pthread_mutex_lock (&net_bh_lock);
  while (1)
    {
      while (!net_bh_raised)
        pthread_cond_wait (&net_bh_wakeup, &net_bh_lock);

      net_bh_raised = 0;

      pthread_mutex_lock (&global_lock);
      net_bh ();
      pthread_mutex_unlock (&global_lock);
    }
  /*NOTREACHED*/
  return 0;
}
