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
#include <assert-backtrace.h>
#include <error.h>
#include <stdio.h>
#include <mach/message.h>
#include <mach/thread_info.h>
#include <mach/thread_switch.h>

#define STACK_SIZE (64 * 1024)

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

  err = get_privileged_ports (&host_priv, NULL);
  if (err == MACH_SEND_INVALID_DEST)
    /* This is returned if we neither have the privileged host control
       port cached nor have a proc server to talk to.  Give up.  */
    return;

  if (err)
    goto error_host_priv;

  self = mach_thread_self ();
  err = thread_get_assignment (self, &pset);
  if (err)
    goto error_pset;

  err = host_processor_set_priv (host_priv, pset, &pset_priv);
  if (err)
    goto error_pset_priv;

  err = thread_max_priority (self, pset_priv, 0);
  /* If we are running in an unprivileged subhurd, we got a faked
     privileged processor set port.  This is indeed a kind of
     permission problem, and we treat it as such.  */
  if (err == KERN_INVALID_ARGUMENT)
    err = EPERM;
  if (err)
    goto error_max_priority;

  err = thread_priority (self, THREAD_PRI, 0);
  if (err)
    goto error_priority;

  mach_port_deallocate (mach_task_self (), pset_priv);
  mach_port_deallocate (mach_task_self (), pset);
  mach_port_deallocate (mach_task_self (), self);
  mach_port_deallocate (mach_task_self (), host_priv);
  return;

error_priority:
error_max_priority:
  mach_port_deallocate (mach_task_self (), pset_priv);
error_pset_priv:
  mach_port_deallocate (mach_task_self (), pset);
error_pset:
  mach_port_deallocate (mach_task_self (), self);
  mach_port_deallocate (mach_task_self (), host_priv);
error_host_priv:
  if (err != EPERM)
    error (0, err, "unable to adjust libports thread priority");
}

void
ports_manage_port_operations_multithread (struct port_bucket *bucket,
					  ports_demuxer_type demuxer,
					  int thread_timeout,
					  int global_timeout,
					  void (*hook)())
{
  /* totalthreads is the number of total threads created.  nreqthreads
     is the number of threads not currently servicing any client.  The
     initial values account for the main thread.  */
  unsigned int totalthreads = 1;
  unsigned int nreqthreads = 1;

  pthread_attr_t attr;

  auto void * thread_function (void *);

  pthread_attr_init (&attr);
  pthread_attr_setstacksize (&attr, STACK_SIZE);

  int
  internal_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outheadp)
    {
      int status;
      struct port_info *pi;
      struct rpc_info link;
      mig_reply_header_t *outp = (mig_reply_header_t *) outheadp;
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

      if (__atomic_sub_fetch (&nreqthreads, 1, __ATOMIC_RELAXED) == 0)
	/* No thread would be listening for requests, spawn one. */
	{
	  pthread_t pthread_id;
	  error_t err;

	  __atomic_add_fetch (&totalthreads, 1, __ATOMIC_RELAXED);
	  __atomic_add_fetch (&nreqthreads, 1, __ATOMIC_RELAXED);

	  err = pthread_create (&pthread_id, &attr, thread_function, NULL);
	  if (!err)
	    pthread_detach (pthread_id);
	  else
	    {
	      __atomic_sub_fetch (&totalthreads, 1, __ATOMIC_RELAXED);
	      __atomic_sub_fetch (&nreqthreads, 1, __ATOMIC_RELAXED);
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

      if (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) ==
	  MACH_MSG_TYPE_PROTECTED_PAYLOAD)
	pi = ports_lookup_payload (bucket, inp->msgh_protected_payload, NULL);
      else
	{
	  pi = ports_lookup_port (bucket, inp->msgh_local_port, 0);
	  if (pi)
	    {
	      /* Store the objects address as the payload and set the
		 message type accordingly.  This prevents us from
		 having to do another hash table lookup in the intran
		 functions if protected payloads are not supported by
		 the kernel.  */
	      inp->msgh_bits =
		MACH_MSGH_BITS_OTHER (inp->msgh_bits)
		| MACH_MSGH_BITS (MACH_MSGH_BITS_REMOTE (inp->msgh_bits),
				  MACH_MSG_TYPE_PROTECTED_PAYLOAD);
	      inp->msgh_protected_payload = (unsigned long) pi;
	    }
	}

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
	      mach_port_seqno_t cancel_threshold =
		__atomic_load_n (&pi->cancel_threshold, __ATOMIC_SEQ_CST);

	      if (inp->msgh_seqno < cancel_threshold)
		hurd_thread_cancel (link.thread);

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

      __atomic_add_fetch (&nreqthreads, 1, __ATOMIC_RELAXED);

      return status;
    }

  void *
  thread_function (void *arg)
    {
      struct ports_thread thread;
      int master = (int) arg;
      int timeout;
      error_t err;

      int synchronized_demuxer (mach_msg_header_t *inp,
				mach_msg_header_t *outheadp)
      {
	int r = internal_demuxer (inp, outheadp);
	_ports_thread_quiescent (&bucket->threadpool, &thread);
	return r;
      }

      adjust_priority (__atomic_load_n (&totalthreads, __ATOMIC_RELAXED));

      if (hook)
	(*hook) ();

      if (master)
	timeout = global_timeout;
      else
	timeout = thread_timeout;

      _ports_thread_online (&bucket->threadpool, &thread);

    startover:

      do
	err = mach_msg_server_timeout (synchronized_demuxer,
				       0, bucket->portset,
				       timeout ? MACH_RCV_TIMEOUT : 0,
				       timeout);
      while (err != MACH_RCV_TIMED_OUT);

      if (master)
	{
	  if (__atomic_load_n (&totalthreads, __ATOMIC_RELAXED) != 1)
	    goto startover;
	}
      else
	{
	  if (__atomic_sub_fetch (&nreqthreads, 1, __ATOMIC_RELAXED) == 0)
	    {
	      /* No other thread is listening for requests, continue. */
	      __atomic_add_fetch (&nreqthreads, 1, __ATOMIC_RELAXED);
	      goto startover;
	    }
	  __atomic_sub_fetch (&totalthreads, 1, __ATOMIC_RELAXED);
	}
      _ports_thread_offline (&bucket->threadpool, &thread);
      return NULL;
    }

  /* XXX It is currently unsafe for most servers to terminate based on
     inactivity because a request may arrive after a server has started
     shutting down, causing the client to receive an error.  Prevent the
     master thread from going away.  */
  global_timeout = 0;

  thread_function ((void *) 1);
}
