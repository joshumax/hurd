/* 
   Copyright (C) 1995, 1996, 2001, 2003 Free Software Foundation, Inc.
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

void
ports_reallocate_port (void *portstruct)
{
  struct port_info *pi = portstruct;
  error_t err;
  int dropref = 0;

  pthread_mutex_lock (&_ports_lock);
  assert_backtrace (pi->port_right);

  err = mach_port_mod_refs (mach_task_self (), pi->port_right, 
			    MACH_PORT_RIGHT_RECEIVE, -1);
  assert_perror_backtrace (err);

  pthread_rwlock_wrlock (&_ports_htable_lock);
  hurd_ihash_locp_remove (&_ports_htable, pi->ports_htable_entry);
  hurd_ihash_locp_remove (&pi->bucket->htable, pi->hentry);
  pthread_rwlock_unlock (&_ports_htable_lock);

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &pi->port_right);
  assert_perror_backtrace (err);
  if (pi->flags & PORT_HAS_SENDRIGHTS)
    {
      pi->flags &= ~PORT_HAS_SENDRIGHTS;
      dropref = 1;
    }
  pi->cancel_threshold = 0;
  pi->mscount = 0;
  pthread_rwlock_wrlock (&_ports_htable_lock);
  err = hurd_ihash_add (&_ports_htable, pi->port_right, pi);
  assert_perror_backtrace (err);
  err = hurd_ihash_add (&pi->bucket->htable, pi->port_right, pi);
  pthread_rwlock_unlock (&_ports_htable_lock);
  pthread_mutex_unlock (&_ports_lock);
  assert_perror_backtrace (err);

  /* This is an optimization.  It may fail.  */
  mach_port_set_protected_payload (mach_task_self (), pi->port_right,
				   (unsigned long) pi);

  err = mach_port_move_member (mach_task_self (), pi->port_right, 
			       pi->bucket->portset);
  assert_perror_backtrace (err);

  if (dropref)
    ports_port_deref (pi);
}
