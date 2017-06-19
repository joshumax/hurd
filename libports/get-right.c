/*
   Copyright (C) 1995,96,2000 Free Software Foundation, Inc.
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
#include <assert-backtrace.h>

mach_port_t
ports_get_right (void *port)
{
  struct port_info *pi = port;
  mach_port_t foo;
  error_t err;

  pthread_mutex_lock (&_ports_lock);

  if (pi->port_right == MACH_PORT_NULL)
    {
      pthread_mutex_unlock (&_ports_lock);
      return MACH_PORT_NULL;
    }

  pi->mscount++;
  if ((pi->flags & PORT_HAS_SENDRIGHTS) == 0)
    {
      pi->flags |= PORT_HAS_SENDRIGHTS;
      refcounts_ref (&pi->refcounts, NULL);
      err = mach_port_request_notification (mach_task_self (),
					    pi->port_right,
					    MACH_NOTIFY_NO_SENDERS,
					    pi->mscount,
					    pi->port_right,
					    MACH_MSG_TYPE_MAKE_SEND_ONCE,
					    &foo);
      assert_perror_backtrace (err);
      if (foo != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), foo);
    }
  pthread_mutex_unlock (&_ports_lock);
  return pi->port_right;
}
