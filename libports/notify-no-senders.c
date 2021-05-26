/* No sender notification

   Copyright (C) 1995, 2021 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>,
   and Sergey Bugaev <bugaevc@gmail.com>.

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
#include "notify_S.h"

error_t
ports_do_mach_notify_no_senders (struct port_info *pi,
                                 mach_port_mscount_t count)
{
  error_t err;
  mach_port_status_t stat;

  if (!pi)
    return EOPNOTSUPP;

  /* Treat the notification as a hint, since it might not be coming from the
     kernel.  We now check if there are indeed no more senders left.  */
  err = mach_port_get_receive_status (mach_task_self (),
                                      pi->port_right, &stat);
  if (err)
    return err;

  if (stat.mps_srights)
    return EAGAIN;

  ports_no_senders (pi, stat.mps_mscount);
  return 0;
}
