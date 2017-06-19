/* ports_get_send_right -- get a send right to a ports object
   Copyright (C) 2000 Free Software Foundation, Inc.

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
#include <assert-backtrace.h>

mach_port_t
ports_get_send_right (void *port)
{
  error_t err;
  mach_port_t right;

  right = ports_get_right (port);
  if (right == MACH_PORT_NULL)
    return MACH_PORT_NULL;

  err = mach_port_insert_right (mach_task_self (),
				right, right, MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);

  return right;
}
