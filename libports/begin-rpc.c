/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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
#include <cthreads.h>

error_t
ports_begin_rpc (void *portstruct, struct rpc_info *info)
{
  struct port_info *pi = portstruct;
  
  mutex_lock (&_ports_lock);
  
 start_over:

  /* If our receive right is gone, then abandon the RPC. */
  if (pi->port_right == MACH_PORT_NULL)
    {
      mutex_unlock (&_ports_lock);
      return EDIED;
    }
  
  /* Check to see if RPC's are inhibited */
  while (_ports_flags & (_PORTS_INHIBITED | _PORTS_INHIBIT_WAIT))
    {
      _ports_flags |= _PORTS_BLOCKED;
      condition_wait (&_ports_block, &_ports_lock);
    }
  
  /* Check to see if RPC's are inhibited for this port's bucket */
  if (pi->bucket->flags & (PORT_BUCKET_INHIBITED | PORT_BUCKET_INHIBIT_WAIT))
    {
      pi->bucket->flags |= PORT_BUCKET_BLOCKED;
      condition_wait (&_ports_block, &_ports_lock);
      goto start_over;
    }

  /* Check to see if RPC's are inhibited for this port's class */
  if (pi->class->flags & (PORT_CLASS_INHIBITED | PORT_CLASS_INHIBIT_WAIT))
    {
      pi->class->flags |= PORT_CLASS_BLOCKED;
      condition_wait (&_ports_block, &_ports_lock);
      goto start_over;
    }
  
  /* Check to see if RPC's are inhibited for this port itself */
  if (pi->flags & (PORT_INHIBITED | PORT_INHIBIT_WAIT))
    {
      pi->flags |= PORT_BLOCKED;
      condition_wait (&_ports_block, &_ports_lock);
      goto start_over;
    }
  
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
  mutex_unlock (&_ports_lock);

  return 0;
}

  
