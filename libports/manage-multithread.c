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
#include <spin-lock.h>
#include <assert.h>
#include <cthreads.h>

void
ports_manage_port_operations_multithread (struct port_bucket *bucket,
					  ports_demuxer_type demuxer,
					  int thread_timeout,
					  int global_timeout,
					  int wire_cthreads,
					  mach_port_t wire_threads)
{
  volatile int nreqthreads;
  volatile int totalthreads;
  spin_lock_t lock = SPIN_LOCK_INITIALIZER;

  auto int thread_function (int);

  int
  internal_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outp)
    {
      int spawn = 0;
      int status;
      struct port_info *pi;
      struct rpc_info link;

      spin_lock (&lock);
      assert (nreqthreads);
      nreqthreads--;
      if (nreqthreads == 0)
	spawn = 1;
      spin_unlock (&lock);

      if (spawn)
	{
	  spin_lock (&lock);
	  totalthreads++;
	  nreqthreads++;
	  spin_unlock (&lock);
	  cthread_detach (cthread_fork ((cthread_fn_t) thread_function, 0));
	}

      pi = ports_lookup_port (bucket, inp->msgh_local_port, 0);
      if (pi)
	{
	  ports_begin_rpc (pi, &link);
	  mutex_lock (&_ports_lock);
	  if (inp->msgh_seqno < pi->cancel_threshhold)
	    hurd_thread_cancel (link.thread);
	  mutex_unlock (&_ports_lock);
	  status = demuxer (inp, outp);
	  ports_end_rpc (pi, &link);
	  ports_port_deref (pi);
	}
      else
	status = 0;

      spin_lock (&lock);
      nreqthreads++;
      spin_unlock (&lock);

      return status;
    }

  int
  thread_function (int master)
    {
      int timeout;
      error_t err;

      if (wire_threads)
	thread_wire (wire_threads, hurd_thread_self (), 1);
      if (wire_cthreads)
	cthread_wire ();

      if (master)
	timeout = global_timeout;
      else
	timeout = thread_timeout;

    startover:

      do
	err = mach_msg_server_timeout (internal_demuxer, 0, bucket->portset,
				       timeout ? MACH_RCV_TIMEOUT : 0,
				       timeout);
      while (err != MACH_RCV_TIMED_OUT);

      if (master)
	{
	  spin_lock (&lock);
	  if (totalthreads != 1)
	    {
	      spin_unlock (&lock);
	      goto startover;
	    }
	}
      else
	{
	  spin_lock (&lock);
	  nreqthreads--;
	  totalthreads--;
	  spin_unlock (&lock);
	}
      return 0;
    }

  /* Wire this because hurd_thread_cancel will not interoperate
     cleanly with cthreads cleverness yet. */
  wire_cthreads = 1;

  nreqthreads = 1;
  totalthreads = 1;
  thread_function (1);
}





