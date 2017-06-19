/*
   Copyright (C) 1995, 1996, 1999, 2014 Free Software Foundation, Inc.
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
#include <hurd/ihash.h>
#include <assert-backtrace.h>

#include <pthread.h>
#include <error.h>
#include <time.h>
#include <unistd.h>

error_t
ports_destroy_right (void *portstruct)
{
  struct port_info *pi = portstruct;
  mach_port_t port_right;
  int defer = 0;
  error_t err;

  pthread_mutex_lock (&_ports_lock);
  port_right = pi->port_right;
  pi->port_right = MACH_PORT_DEAD;

  if (pi->flags & PORT_HAS_SENDRIGHTS)
    {
      pi->flags &= ~PORT_HAS_SENDRIGHTS;

      /* There are outstanding send rights, so we might get more
         messages.  Attached to the messages is a reference to the
         port_info object.  Of course we destroyed the receive right
         these were send to above, but the message could already have
         been dequeued to userspace.

         Previously, those messages would have carried an stale name,
         which would have caused a hash table lookup failure.
         However, stale payloads results in port_info use-after-free.
         Therefore, we cannot release the reference here, but defer
         that instead until all currently running threads have gone
         through a quiescent state.  */
      defer = 1;
    }

  if (MACH_PORT_VALID (port_right))
    {
      mach_port_clear_protected_payload (mach_task_self (), port_right);

      pthread_rwlock_wrlock (&_ports_htable_lock);
      hurd_ihash_locp_remove (&_ports_htable, pi->ports_htable_entry);
      hurd_ihash_locp_remove (&pi->bucket->htable, pi->hentry);
      pthread_rwlock_unlock (&_ports_htable_lock);
    }
  pthread_mutex_unlock (&_ports_lock);

  if (MACH_PORT_VALID (port_right))
    {
      err = mach_port_mod_refs (mach_task_self (), port_right,
                                MACH_PORT_RIGHT_RECEIVE, -1);
      assert_perror_backtrace (err);
    }

  if (defer)
    _ports_port_deref_deferred (pi);

  return 0;
}
