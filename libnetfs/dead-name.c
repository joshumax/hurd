/* Handle dead name notifications on ports
   Copyright (C) 1996 Free Software Foundation, Inc.
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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "priv.h"

void
ports_dead_name (void *notify, mach_port_t dead_name)
{
  struct protid *pi = ports_lookup_port (netfs_port_bucket, dead_name,
					 netfs_protid_class);
  struct node *np;

  if (pi)
    {
      np = pi->po->np;
      pthread_mutex_lock (&np->lock);
      if (dead_name == np->sockaddr)
	{
	  mach_port_deallocate (mach_task_self (), np->sockaddr);
	  np->sockaddr = MACH_PORT_NULL;
	  netfs_nput (np);
	}
      else
	pthread_mutex_unlock (&np->lock);
    }

  fshelp_remove_active_translator (dead_name);

  ports_interrupt_notified_rpcs (notify, dead_name, MACH_NOTIFY_DEAD_NAME);
}
