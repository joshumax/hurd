/*
   Copyright (C) 1995, 1996, 1999 Free Software Foundation, Inc.
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
#include <cthreads.h>
#include <hurd/ihash.h>
#include <assert.h>

error_t
ports_destroy_right (void *portstruct)
{
  struct port_info *pi = portstruct;
  error_t err;

  if (pi->port_right != MACH_PORT_NULL)
    {
      mutex_lock (&_ports_lock);
      hurd_ihash_locp_remove (&pi->bucket->htable, pi->hentry);
      err = mach_port_mod_refs (mach_task_self (), pi->port_right,
				MACH_PORT_RIGHT_RECEIVE, -1);
      assert_perror (err);
      mutex_unlock (&_ports_lock);

      pi->port_right = MACH_PORT_NULL;

      if (pi->flags & PORT_HAS_SENDRIGHTS)
	{
	  pi->flags &= ~PORT_HAS_SENDRIGHTS;
	  ports_port_deref (pi);
	}
    }

  return 0;
}
