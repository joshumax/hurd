/* Create a new port structure using an externally supplied receive right

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
#include <assert-backtrace.h>
#include <hurd/ihash.h>
#include <mach/notify.h>

/* For an existing receive right PORT, create and return in RESULT a new port
   structure; BUCKET, SIZE, and CLASS args are as for ports_create_port. */
error_t
ports_import_port (struct port_class *class, struct port_bucket *bucket,
		   mach_port_t port, size_t size, void *result)
{
  error_t err;
  mach_port_status_t stat;
  struct port_info *pi;
  mach_port_t foo;
  
  err = mach_port_get_receive_status (mach_task_self (), port, &stat);
  if (err)
    return err;

  if (size < sizeof (struct port_info))
    size = sizeof (struct port_info);
  
  pi = malloc (size);
  if (! pi)
    return ENOMEM;
  
  pi->class = class;
  refcounts_init (&pi->refcounts, 1 + !!stat.mps_srights, 0);
  pi->cancel_threshold = 0;
  pi->mscount = stat.mps_mscount;
  pi->flags = stat.mps_srights ? PORT_HAS_SENDRIGHTS : 0;
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

  mach_port_move_member (mach_task_self (), port, bucket->portset);

  if (stat.mps_srights)
    {
      err = mach_port_request_notification (mach_task_self (), port,
					    MACH_NOTIFY_NO_SENDERS, 
					    stat.mps_mscount, 
					    port, MACH_MSG_TYPE_MAKE_SEND_ONCE,
					    &foo);
      assert_perror_backtrace (err);
      if (foo != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), foo);
    }

  *(void **)result = pi;
  return 0;

 cancelled:
  err = EINTR;
 lose:
  pthread_mutex_unlock (&_ports_lock);
  free (pi);

  return err;
}
