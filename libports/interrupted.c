/* Keeping track of thread interruption

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include "ports.h"

static pthread_spinlock_t interrupted_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* True if some active rpc has been interrupted.  */
static struct rpc_info *interrupted = 0;

/* If the current thread's rpc has been interrupted with
   ports_interrupt_rpcs, return true (and clear the interrupted flag).  */
int
ports_self_interrupted ()
{
  struct rpc_info **rpc_p, *rpc;
  thread_t self = hurd_thread_self ();

  pthread_spin_lock (&interrupted_lock);
  for (rpc_p = &interrupted; *rpc_p; rpc_p = &rpc->interrupted_next)
    {
      rpc = *rpc_p;
      if (rpc->thread == self)
	{
	  *rpc_p = rpc->interrupted_next;
	  pthread_spin_unlock (&interrupted_lock);
	  rpc->interrupted_next = 0;
	  return 1;
	}
    }
  pthread_spin_unlock (&interrupted_lock);

  return 0;
}

/* Add RPC to the list of rpcs that have been interrupted.  */
void
_ports_record_interruption (struct rpc_info *rpc)
{
  struct rpc_info *i;

  pthread_spin_lock (&interrupted_lock);

  /* See if RPC is already in the interrupted list.  */
  for (i = interrupted; i; i = i->interrupted_next)
    if (i == rpc)
      /* Yup, it is, so just leave it there.  */
      {
	pthread_spin_unlock (&interrupted_lock);
	return;
      }

  /* Nope, put it at the beginning.  */
  rpc->interrupted_next = interrupted;
  interrupted = rpc;

  pthread_spin_unlock (&interrupted_lock);
}
