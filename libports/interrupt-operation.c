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
ports_S_interrupt_operation (struct port_info *pi,
			     mach_port_seqno_t seqno)
{
  mach_port_seqno_t old;

  if (!pi)
    return EOPNOTSUPP;

 retry:
  old = __atomic_load_n (&pi->cancel_threshold, __ATOMIC_SEQ_CST);
  if (old < seqno
      && ! __atomic_compare_exchange_n (&pi->cancel_threshold, &old, seqno,
					0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
	goto retry;

  ports_interrupt_rpcs (pi);
  return 0;
}
