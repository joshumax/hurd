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

#include <lwip_startup_notify_S.h>

#include <lwip-hurd.h>

/* The system is going down; destroy all the extant port rights.  That
   will cause net channels and such to close promptly.  */
error_t
lwip_S_startup_dosync (mach_port_t handle)
{
  struct port_info *inpi = ports_lookup_port (lwip_bucket, handle,
					      shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  ports_class_iterate (socketport_class, ports_destroy_right);
  ports_class_iterate (addrport_class, ports_destroy_right);
  return 0;
}
