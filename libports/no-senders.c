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
#include <mach/notify.h>

void
ports_no_senders (void *portstruct,
		  mach_port_mscount_t mscount)
{
  struct port_info *pi = portstruct;
  int dealloc;
  mach_port_t old;

  pthread_mutex_lock (&_ports_lock);
  if ((pi->flags & PORT_HAS_SENDRIGHTS) == 0)
    {
      pthread_mutex_unlock (&_ports_lock);
      return;
    }
  if (mscount >= pi->mscount)
    {
      dealloc = 1;
      pi->flags &= ~PORT_HAS_SENDRIGHTS;
    }
  else
    {
      /* Request a new notification.  The sync value is because
       we might have accounted for a new sender but not actually
       made the send right yet.  */
      mach_port_request_notification (mach_task_self (), pi->port_right,
				      MACH_NOTIFY_NO_SENDERS, pi->mscount,
				      pi->port_right,
				      MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
      if (old)
	mach_port_deallocate (mach_task_self (), old);
      dealloc = 0;
    }
  pthread_mutex_unlock (&_ports_lock);
  
  if (dealloc)
    {
      ports_interrupt_notified_rpcs (portstruct, pi->port_right,
				     MACH_NOTIFY_NO_SENDERS);
      ports_interrupt_rpcs (pi);
      ports_port_deref (pi);
    }
}
