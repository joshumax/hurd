/* 
   Copyright (C) 1996,2001 Free Software Foundation, Inc.
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

#include <assert-backtrace.h>
#include <hurd/ihash.h>
#include "ports.h"

/* Create and return in RESULT a new port in CLASS and BUCKET; SIZE
   bytes will be allocated to hold the port structure and whatever
   private data the user desires.  If INSTALL is set, put the port
   right into BUCKET's port set.  */
error_t
_ports_create_port_internal (struct port_class *class, 
			     struct port_bucket *bucket,
			     size_t size, void *result,
			     int install)
{
  mach_port_t port;
  error_t err;
  struct port_info *pi;

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &port);
  if (err)
    return err;

  if (size < sizeof (struct port_info))
    size = sizeof (struct port_info);

  pi = malloc (size);
  if (! pi)
    {
      err = mach_port_mod_refs (mach_task_self (), port,
				MACH_PORT_RIGHT_RECEIVE, -1);
      assert_perror_backtrace (err);
      return ENOMEM;
    }

  pi->class = class;
  refcounts_init (&pi->refcounts, 1, 0);
  pi->cancel_threshold = 0;
  pi->mscount = 0;
  pi->flags = 0;
  pi->port_right = port;
  pi->current_rpcs = 0;
  pi->bucket = bucket;
  
  pthread_mutex_lock (&_ports_lock);
  
 loop:
  if (class->flags & PORT_CLASS_NO_ALLOC)
    { 
      class->flags |= PORT_CLASS_ALLOC_WAIT;
      if (pthread_hurd_cond_wait_np (&_ports_block, &_ports_lock))
	goto cancelled;
      goto loop;
    }
  if (bucket->flags & PORT_BUCKET_NO_ALLOC)
    {
      bucket->flags |= PORT_BUCKET_ALLOC_WAIT;
      if (pthread_hurd_cond_wait_np (&_ports_block, &_ports_lock))
	goto cancelled;
      goto loop;
    }

  pthread_rwlock_wrlock (&_ports_htable_lock);
  err = hurd_ihash_add (&_ports_htable, port, pi);
  if (err)
    {
      pthread_rwlock_unlock (&_ports_htable_lock);
      goto lose;
    }
  err = hurd_ihash_add (&bucket->htable, port, pi);
  if (err)
    {
      hurd_ihash_locp_remove (&_ports_htable, pi->ports_htable_entry);
      pthread_rwlock_unlock (&_ports_htable_lock);
      goto lose;
    }
  pthread_rwlock_unlock (&_ports_htable_lock);

  bucket->count++;
  class->count++;
  pthread_mutex_unlock (&_ports_lock);

  /* This is an optimization.  It may fail.  */
  mach_port_set_protected_payload (mach_task_self (), port,
				   (unsigned long) pi);

  if (install)
    {
      err = mach_port_move_member (mach_task_self (), pi->port_right,
				   bucket->portset);
      if (err)
	goto lose_unlocked;
    }

  *(void **)result = pi;
  return 0;

 cancelled:
  err = EINTR;
 lose:
  pthread_mutex_unlock (&_ports_lock);
 lose_unlocked:;
  error_t e;
  e = mach_port_mod_refs (mach_task_self (), port,
			  MACH_PORT_RIGHT_RECEIVE, -1);
  assert_perror_backtrace (e);
  free (pi);

  return err;
}
