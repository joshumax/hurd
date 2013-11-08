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
#include <assert.h>
#include <stdio.h>
#include <mach/message.h>
#include <mach/thread_info.h>
#include <mach/thread_switch.h>

#define STACK_SIZE (64 * 1024)

/* FIXME Until threadvars are completely replaced with correct TLS, use this
   hack to set the stack size.  */
size_t __pthread_stack_default_size = STACK_SIZE;

#define THREAD_PRI 2

/* XXX To reduce starvation, the priority of new threads is initially
   depressed. This helps already existing threads complete their job and be
   recycled to handle new messages. The duration of this depression is made
   a function of the total number of threads because more threads imply
   more contention, and the priority of threads blocking on a contended spin
   lock is also implicitely depressed.

   Then, if permitted, a greater priority is requested to further decrease
   the need for additional threads. */
static void
adjust_priority (unsigned int totalthreads)
{
  mach_port_t host_priv, self, pset, pset_priv;
  unsigned int t;
  error_t err;

  t = 10 + (((totalthreads - 1) / 100) + 1) * 10;
  thread_switch (MACH_PORT_NULL, SWITCH_OPTION_DEPRESS, t);

  self = MACH_PORT_NULL;

  err = get_privileged_ports (&host_priv, NULL);
  if (err)
    goto out;

  self = mach_thread_self ();
  err = thread_get_assignment (self, &pset);
  if (err)
    goto out;

  err = host_processor_set_priv (host_priv, pset, &pset_priv);
  if (err)
    goto out;

  err = thread_max_priority (self, pset_priv, 0);
  if (err)
    goto out;

  err = thread_priority (self, THREAD_PRI, 0);

out:
  if (self != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), self);

  if (err && err != EPERM)
    {
      errno = err;
      perror ("unable to adjust libports thread priority");
    }
}

void
ports_manage_port_operations_multithread (struct port_bucket *bucket,
					  ports_demuxer_type demuxer,
					  int thread_timeout,
					  int global_timeout,
					  void (*hook)())
{
  volatile unsigned int nreqthreads;
  volatile unsigned int totalthreads;
  pthread_spinlock_t lock = PTHREAD_SPINLOCK_INITIALIZER;
  pthread_attr_t attr;

  auto void * thread_function (void *);

  /* FIXME This is currently a no-op.  */
  pthread_attr_init (&attr);
  pthread_attr_setstacksize (&attr, STACK_SIZE);

  int
  internal_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outheadp)
    {
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

      pthread_spin_lock (&lock);
      assert (nreqthreads);
      nreqthreads--;
      if (nreqthreads != 0)
	pthread_spin_unlock (&lock);
      else
	/* No thread would be listening for requests, spawn one. */
	{
	  pthread_t pthread_id;
	  error_t err;

	  totalthreads++;
	  nreqthreads++;
	  pthread_spin_unlock (&lock);

	  err = pthread_create (&pthread_id, &attr, thread_function, NULL);
	  if (!err)
	    pthread_detach (pthread_id);
	  else
	    {
	      pthread_spin_lock (&lock);
	      totalthreads--;
	      nreqthreads--;
	      pthread_spin_unlock (&lock);
	      /* There is not much we can do at this point.  The code
		 and design of the Hurd servers just don't handle
		 thread creation failure.  */
	      errno = err;
	      perror ("pthread_create");
	    }
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
	      pthread_mutex_lock (&_ports_lock);
	      if (inp->msgh_seqno < pi->cancel_threshold)
		hurd_thread_cancel (link.thread);
	      pthread_mutex_unlock (&_ports_lock);
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

      pthread_spin_lock (&lock);
      nreqthreads++;
      pthread_spin_unlock (&lock);

      return status;
    }

  void *
  thread_function (void *arg)
    {
      int master = (int) arg;
      int timeout;
      error_t err;

      /* No need to lock as an approximation is sufficient. */
      adjust_priority (totalthreads);

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
	  pthread_spin_lock (&lock);
	  if (totalthreads != 1)
	    {
	      pthread_spin_unlock (&lock);
	      goto startover;
	    }
	}
      else
	{
	  pthread_spin_lock (&lock);
	  if (nreqthreads == 1)
	    {
	      /* No other thread is listening for requests, continue. */
	      pthread_spin_unlock (&lock);
	      goto startover;
	    }
	  nreqthreads--;
	  totalthreads--;
	  pthread_spin_unlock (&lock);
	}
      return NULL;
    }

  nreqthreads = 1;
  totalthreads = 1;
  thread_function ((void *) 1);
}
