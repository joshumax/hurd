/*
   Copyright (C) 1995 Free Software Foundation, Inc.
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
#include <hurd.h>
#include <cthreads.h>
#include <hurd/ihash.h>

void
ports_inhibit_bucket_rpcs (struct port_bucket *bucket)
{
  int this_one = 0;

  error_t interruptor (void *portstruct)
    {
      struct port_info *pi = portstruct;
      struct rpc_info *rpc;

      for (rpc = pi->current_rpcs; rpc; rpc = rpc->next)
	if (hurd_thread_cancel (rpc->thread) == EINTR)
	  this_one = 1;
      return 0;
    }

  mutex_lock (&_ports_lock);

  ihash_iterate (bucket->htable, interruptor);

  while (bucket->rpcs > this_one)
    {
      bucket->flags |= PORT_BUCKET_INHIBIT_WAIT;
      condition_wait (&_ports_block, &_ports_lock);
    }

  bucket->flags |= PORT_BUCKET_INHIBITED;
  bucket->flags &= ~PORT_CLASS_INHIBIT_WAIT;

  mutex_unlock (&_ports_lock);
}
