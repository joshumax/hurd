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
#include <assert.h>
#include <cthreads.h>
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
  pi->refcnt = 1 + !!stat.mps_srights;
  pi->weakrefcnt = 0;
  pi->cancel_threshold = 0;
  pi->mscount = stat.mps_mscount;
  pi->flags = stat.mps_srights ? PORT_HAS_SENDRIGHTS : 0;
  pi->port_right = port;
  pi->current_rpcs = 0;
  pi->bucket = bucket;
  
  mutex_lock (&_ports_lock);
  
 loop:
  if (class->flags & PORT_CLASS_NO_ALLOC)
    { 
      class->flags |= PORT_CLASS_ALLOC_WAIT;
      if (hurd_condition_wait (&_ports_block, &_ports_lock))
	goto cancelled;
      goto loop;
    }
  if (bucket->flags & PORT_BUCKET_NO_ALLOC)
    {
      bucket->flags |= PORT_BUCKET_ALLOC_WAIT;
      if (hurd_condition_wait (&_ports_block, &_ports_lock))
	goto cancelled;
      goto loop;
    }

  err = hurd_ihash_add (&bucket->htable, port, pi);
  if (err)
    goto lose;

  pi->next = class->ports;
  pi->prevp = &class->ports;
  if (class->ports)
    class->ports->prevp = &pi->next;
  class->ports = pi;
  bucket->count++;
  class->count++;
  mutex_unlock (&_ports_lock);
  
  mach_port_move_member (mach_task_self (), port, bucket->portset);

  if (stat.mps_srights)
    {
      err = mach_port_request_notification (mach_task_self (), port,
					    MACH_NOTIFY_NO_SENDERS, 
					    stat.mps_mscount, 
					    port, MACH_MSG_TYPE_MAKE_SEND_ONCE,
					    &foo);
      assert_perror (err);
      if (foo != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), foo);
    }

  *(void **)result = pi;
  return 0;

 cancelled:
  err = EINTR;
 lose:
  mutex_unlock (&_ports_lock);
  free (pi);

  return err;
}
