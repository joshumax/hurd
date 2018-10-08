/* 
   Copyright (C) 1995, 1996, 1997, 1999 Free Software Foundation, Inc.
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

void
ports_end_rpc (void *port, struct rpc_info *info)
{
  struct port_info *pi = port;

  pthread_mutex_lock (&_ports_lock);

  if (info->notifies)
    _ports_remove_notified_rpc (info);

  *info->prevp = info->next;
  if (info->next)
    info->next->prevp = info->prevp;
  pi->class->rpcs--;
  _ports_total_rpcs--;
  pi->bucket->rpcs--;

  if ((pi->flags & PORT_INHIBIT_WAIT)
      || (pi->bucket->flags & PORT_BUCKET_INHIBIT_WAIT)
      || (pi->class->flags & PORT_CLASS_INHIBIT_WAIT)
      || (_ports_flags & _PORTS_INHIBIT_WAIT))
    pthread_cond_broadcast (&_ports_block);

  /* This removes the current thread's rpc (which should be INFO) from the
     ports interrupted list.  */
  ports_self_interrupted ();

  /* Clear the cancellation flag for this thread since the current 
     RPC is now finished anyhow. */
  hurd_check_cancel ();

  pthread_mutex_unlock (&_ports_lock);
}
