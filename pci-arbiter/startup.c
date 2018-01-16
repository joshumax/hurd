/*
   Copyright (C) 2017 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Startup and shutdown notifications management */

#include "startup.h"

#include <unistd.h>
#include <hurd/paths.h>
#include <hurd/startup.h>
#include <hurd/netfs.h>

void
arrange_shutdown_notification ()
{
  error_t err;
  mach_port_t initport, notify;
  struct port_info *pi;

  pci_shutdown_notify_class = ports_create_class (0, 0);

  /* Arrange to get notified when the system goes down,
     but if we fail for some reason, just silently give up.  No big deal. */

  err = ports_create_port (pci_shutdown_notify_class, netfs_port_bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return;

  initport = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (initport == MACH_PORT_NULL)
    return;

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  startup_request_notification (initport, notify,
				MACH_MSG_TYPE_MAKE_SEND,
				program_invocation_short_name);
  mach_port_deallocate (mach_task_self (), notify);
  mach_port_deallocate (mach_task_self (), initport);
}
