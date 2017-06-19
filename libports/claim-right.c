/* Take a receive right away from a port
   Copyright (C) 1996, 2001 Free Software Foundation, Inc.
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
#include <assert-backtrace.h>
#include <errno.h>
#include <hurd/ihash.h>

mach_port_t
ports_claim_right (void *portstruct)
{
  error_t err;
  struct port_info *pi = portstruct;
  mach_port_t ret = pi->port_right;

  if (ret == MACH_PORT_NULL)
    return ret;

  pthread_rwlock_wrlock (&_ports_htable_lock);
  hurd_ihash_locp_remove (&_ports_htable, pi->ports_htable_entry);
  hurd_ihash_locp_remove (&pi->bucket->htable, pi->hentry);
  pthread_rwlock_unlock (&_ports_htable_lock);
  err = mach_port_move_member (mach_task_self (), ret, MACH_PORT_NULL);
  assert_perror_backtrace (err);
  pthread_mutex_lock (&_ports_lock);
  pi->port_right = MACH_PORT_NULL;
  if (pi->flags & PORT_HAS_SENDRIGHTS)
    {
      pi->flags &= ~PORT_HAS_SENDRIGHTS;
      pthread_mutex_unlock (&_ports_lock);
      ports_port_deref (pi);
    }
  else
    pthread_mutex_unlock (&_ports_lock);

  return ret;
}
