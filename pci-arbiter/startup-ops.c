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

#include <startup_notify_S.h>

#include <pciaccess.h>
#include <hurd/netfs.h>

#include "startup.h"

/* The system is going down. Call netfs_shutdown() */
error_t
S_startup_dosync (mach_port_t handle)
{
  struct port_info *inpi = ports_lookup_port (netfs_port_bucket, handle,
					      pci_shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  // Free all libpciaccess resources
  pci_system_cleanup ();

  ports_port_deref (inpi);

  return netfs_shutdown (FSYS_GOAWAY_FORCE);
}
