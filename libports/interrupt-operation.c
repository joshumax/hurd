/* interrupt_operation

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ports.h"
#include "interrupt_S.h"

/* Cause a pending request on this object to immediately return.  The
   exact semantics are dependent on the specific object.  */
kern_return_t
ports_S_interrupt_operation (mach_port_t port,
			     mach_port_seqno_t seqno)
{
  struct port_info *pi = ports_lookup_port (0, port, 0);
  if (!pi)
    return EOPNOTSUPP;
  pthread_mutex_lock (&_ports_lock);
  if (pi->cancel_threshold < seqno)
    pi->cancel_threshold = seqno;
  pthread_mutex_unlock (&_ports_lock);
  ports_interrupt_rpcs (pi);
  ports_port_deref (pi);
  return 0;
}
