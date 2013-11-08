/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include <assert.h>
#include <hurd/ihash.h>

void
_ports_complete_deallocate (struct port_info *pi)
{
  assert ((pi->flags & PORT_HAS_SENDRIGHTS) == 0);

  if (pi->port_right)
    {
      hurd_ihash_locp_remove (&pi->bucket->htable, pi->hentry);
      mach_port_mod_refs (mach_task_self (), pi->port_right,
			  MACH_PORT_RIGHT_RECEIVE, -1);
      pi->port_right = MACH_PORT_NULL;
    }

  *pi->prevp = pi->next;
  if (pi->next)
    pi->next->prevp = pi->prevp;

  pi->bucket->count--;
  pi->class->count--;

  pthread_mutex_unlock (&_ports_lock);

  if (pi->class->clean_routine)
    (*pi->class->clean_routine)(pi);
  
  free (pi);
}
