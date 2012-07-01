/*
   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
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
#include <mach/message.h>

void
ports_manage_port_operations_multithread (struct port_bucket *bucket,
					  ports_demuxer_type demuxer,
					  int thread_timeout,
					  int global_timeout,
					  void (*hook)())
{
  volatile int nreqthreads;
  volatile int totalthreads;
  spin_lock_t lock = SPIN_LOCK_INITIALIZER;

  auto int thread_function (int);

  int
  internal_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outheadp)
    {
      int spawn = 0;
      int status;
      struct port_info *pi;
      struct rpc_info link;
      register mig_reply_header_t *outp = (mig_reply_header_t *) outheadp;
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

      spin_lock (&lock);
      assert (nreqthreads);
      nreqthreads--;
      if (nreqthreads == 0)
	/* No thread would be listening for requests, spawn one. */
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
      
      /* Fill in default response. */
      outp->Head.msgh_bits 
	= MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(inp->msgh_bits), 0);
      outp->Head.msgh_size = sizeof *outp;
      outp->Head.msgh_remote_port = inp->msgh_remote_port;
      outp->Head.msgh_local_port = MACH_PORT_NULL;
      outp->Head.msgh_seqno = 0;
      outp->Head.msgh_id = inp->msgh_id + 100;
      outp->RetCodeType = RetCodeType;
      outp->RetCode = MIG_BAD_ID;

      pi = ports_lookup_port (bucket, inp->msgh_local_port, 0);
      if (pi)
	{
	  error_t err = ports_begin_rpc (pi, inp->msgh_id, &link);
	  if (err)
	    {
	      outp->RetCode = err;
	      status = 1;
	    }
	  else
	    {
	      mutex_lock (&_ports_lock);
	      if (inp->msgh_seqno < pi->cancel_threshold)
		hurd_thread_cancel (link.thread);
	      mutex_unlock (&_ports_lock);
	      status = demuxer (inp, outheadp);
	      ports_end_rpc (pi, &link);
	    }
	  ports_port_deref (pi);
	}
      else
	{
	  outp->RetCode = EOPNOTSUPP;
	  status = 1;
	}

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

      if (hook)
	(*hook) ();

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
	  if (nreqthreads == 1)
	    {
	      /* No other thread is listening for requests, continue. */
	      spin_unlock (&lock);
	      goto startover;
	    }
	  nreqthreads--;
	  totalthreads--;
	  spin_unlock (&lock);
	}
      return 0;
    }

  nreqthreads = 1;
  totalthreads = 1;
  thread_function (1);
}





