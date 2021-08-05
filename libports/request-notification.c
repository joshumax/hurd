/* Request dead-name notifications

   Copyright (C) 2021 Free Software Foundation, Inc.

   Written by Sergey Bugaev <bugaevc@gmail.com>

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
#include <mach/notify.h>

error_t
ports_request_dead_name_notification (void *object, mach_port_t name,
                                      mach_port_t *previous)
{
  error_t err;
  mach_port_t notify_port;
  mach_port_t prev;

  if (object)
    notify_port = ports_port_notify_right (object);
  else
    notify_port = MACH_PORT_NULL;

  err = mach_port_request_notification (mach_task_self (), name,
                                        MACH_NOTIFY_DEAD_NAME, 1,
                                        notify_port,
                                        MACH_MSG_TYPE_MAKE_SEND_ONCE,
                                        &prev);
  if (err)
    return err;

  if (previous != NULL)
    *previous = prev;
  else if (MACH_PORT_VALID (prev))
    {
      err = mach_port_deallocate (mach_task_self (), prev);
      assert_perror_backtrace (err);
    }

  return 0;
}
