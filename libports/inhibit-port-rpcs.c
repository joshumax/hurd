/*
   Copyright (C) 1995, 1996, 2000 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ports.h"
#include <hurd.h>

error_t
ports_inhibit_port_rpcs (void *portstruct)
{
  error_t err = 0;
  struct port_info *pi = portstruct;

  pthread_mutex_lock (&_ports_lock);

  if (pi->flags & (PORT_INHIBITED | PORT_INHIBIT_WAIT))
    err = EBUSY;
  else
    {
      struct rpc_info *rpc;
      struct rpc_info *this_rpc = 0;
  
      for (rpc = pi->current_rpcs; rpc; rpc = rpc->next)
	{
	  /* Avoid cancelling the calling thread.  */
	  if (rpc->thread == hurd_thread_self ())
	    this_rpc = rpc;
	  else
	    hurd_thread_cancel (rpc->thread);
	}

      while (pi->current_rpcs
	     /* If this thread's RPC is the only one left, it doesn't count. */
	     && !(pi->current_rpcs == this_rpc && ! this_rpc->next))
	{
	  pi->flags |= PORT_INHIBIT_WAIT;
	  if (pthread_hurd_cond_wait_np (&_ports_block, &_ports_lock))
	    /* We got cancelled.  */
	    {
	      err = EINTR;
	      break;
	    }
	}

      pi->flags &= ~PORT_INHIBIT_WAIT;
      if (! err)
	pi->flags |= PORT_INHIBITED;
    }

  pthread_mutex_unlock (&_ports_lock);

  return err;
}
