/* Take a receive right away from a port
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


#include "ports.h"
#include <hurd/ihash.h>

mach_port_t
ports_claim_right (void *portstruct)
{
  struct port_info *pi = portstruct;
  mach_port_t ret;

  if (pi->port_right != MACH_PORT_NULL)
    {
      ret = pi->port_right;
      
      mutex_lock (&_ports_lock);
      ihash_locp_remove (pi->bucket->htable, pi->hentry);
      pi->port_right = MACH_PORT_NULL;
      if (pi->flags & PORT_HAS_SENDRIGHTS)
	{
	  pi->flags &= ~PORT_HAS_SENDRIGHTS;
	  ports_port_deref (pi);
	}
      mutex_unlock (&_ports_lock);
    }
  else
    ret = MACH_PORT_NULL;
  return ret;
}

