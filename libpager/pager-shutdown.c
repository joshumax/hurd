/* Pager shutdown in pager library
   Copyright (C) 1994 Free Software Foundation

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

#include "priv.h"
#include <mach/notify.h>

/* Shutdown pager P and prevent any future paging activity on it.  */
void
pager_shutdown (struct pager *p)
{
  mach_port_t port;
  
  /* Sync and flush pager */
  pager_sync (p, 1);
  pager_flush (p, 1);
  mutex_lock (&p->interlock);
  p->pager_state = SHUTDOWN;

  /* Cancel the pending no-senders notification. */
  mach_port_request_notification (mach_task_self (), p->port.port, 
				  MACH_NOTIFY_NO_SENDERS, 0, MACH_PORT_NULL,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &port);
  
  mutex_unlock (&p->interlock);
  if (port)
    {
      done_with_port (p);		/* pretend send right has died */
      mach_port_deallocate (mach_task_self (), port);
    }
}

