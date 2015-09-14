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
#include <hurd/ihash.h>

error_t
ports_inhibit_all_rpcs ()
{
  error_t err = 0;

  pthread_mutex_lock (&_ports_lock);

  if (_ports_flags & (_PORTS_INHIBITED | _PORTS_INHIBIT_WAIT))
    err = EBUSY;
  else
    {
      int this_one = 0;

      pthread_rwlock_rdlock (&_ports_htable_lock);
      HURD_IHASH_ITERATE (&_ports_htable, portstruct)
	{
	  struct rpc_info *rpc;
	  struct port_info *pi = portstruct;

	  for (rpc = pi->current_rpcs; rpc; rpc = rpc->next)
	    {
	      /* Avoid cancelling the calling thread if it's currently
		 handling a RPC.  */
	      if (rpc->thread == hurd_thread_self ())
		this_one = 1;
	      else
		hurd_thread_cancel (rpc->thread);
	    }
	}
      pthread_rwlock_unlock (&_ports_htable_lock);

      while (_ports_total_rpcs > this_one)
	{
	  _ports_flags |= _PORTS_INHIBIT_WAIT;
	  if (pthread_hurd_cond_wait_np (&_ports_block, &_ports_lock))
	    /* We got cancelled.  */
	    {
	      err = EINTR;
	      break;
	    }
	}

      _ports_flags &= ~_PORTS_INHIBIT_WAIT;
      if (! err)
	_ports_flags |= _PORTS_INHIBITED;
    }

  pthread_mutex_unlock (&_ports_lock);

  return err;
}
