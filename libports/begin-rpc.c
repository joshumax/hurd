/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#define INHIBITED (PORTS_INHIBITED | PORTS_INHIBIT_WAIT)

error_t
ports_begin_rpc (void *portstruct, mach_msg_id_t msg_id, struct rpc_info *info)
{
  int *block_flags = 0;

  struct port_info *pi = portstruct;
  
  pthread_mutex_lock (&_ports_lock);
  
  do
    {
      /* If our receive right is gone, then abandon the RPC. */
      if (pi->port_right == MACH_PORT_NULL)
	{
	  pthread_mutex_unlock (&_ports_lock);
	  return EOPNOTSUPP;
	}
  
      if (_ports_flags & INHIBITED)
	/* All RPC's are inhibited.  */
	block_flags = &_ports_flags;
      else if (pi->bucket->flags & INHIBITED)
	/* RPC's are inhibited for this port's bucket.  */
	block_flags = &pi->bucket->flags;
      else if (pi->class->flags & INHIBITED)
	/* RPC's are inhibited for this port's class.  */
	block_flags = &pi->class->flags;
      else if (pi->flags & INHIBITED)
	/* RPC's are inhibited for this port itself.  */
	block_flags = &pi->flags;
      else
	block_flags = 0;

      if (block_flags)
	/* We maybe want to block.  */
	{
	  if (msg_id)
	    /* See if this particular RPC shouldn'be be inhibitable.  */
	    {
	      struct ports_msg_id_range *range = pi->class->uninhibitable_rpcs;
	      while (range)
		if (msg_id >= range->start && msg_id < range->end)
		  {
		    block_flags = 0;
		    break;
		  }
		else
		  range = range->next;
	    }

	  if (block_flags)
	    {
	      *block_flags |= PORTS_BLOCKED;
	      if (pthread_hurd_cond_wait_np (&_ports_block, &_ports_lock))
		/* We've been cancelled, just return EINTR.  If we were the
		   only one blocking, PORTS_BLOCKED will still be turned on,
		   but that's ok, it will just cause a (harmless) extra
		   pthread_cond_broadcast().  */
		{
		  pthread_mutex_unlock (&_ports_lock);
		  return EINTR;
		}
	    }
	}
    }
  while (block_flags);
  
  /* Record that that an RPC is in progress */
  info->thread = hurd_thread_self ();
  info->next = pi->current_rpcs;
  info->notifies = 0;
  if (pi->current_rpcs)
    pi->current_rpcs->prevp = &info->next;
  info->prevp = &pi->current_rpcs;
  pi->current_rpcs = info;

  pi->class->rpcs++;
  pi->bucket->rpcs++;
  _ports_total_rpcs++;

  pthread_mutex_unlock (&_ports_lock);

  return 0;
}
