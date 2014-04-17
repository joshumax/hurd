/* Demuxer for pager library
   Copyright (C) 1993-2014 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <error.h>
#include <mach/mig_errors.h>
#include <pthread.h>
#include <string.h>

#include "priv.h"
#include "memory_object_S.h"
#include "notify_S.h"
#include "queue.h"

/*
  Worker pool for the server functions.

  A single thread receives messages from the port bucket and puts them
  into a queue.  A fixed number of consumers actually execute the
  server functions and send the reply.

  The requests to an object O have to be processed in the order they
  were received.  To this end, each worker has a local queue and a
  tag.  If a thread processes a request to O, it sets its tag to a
  unique identifier representing O.  If another thread now dequeues a
  second request to O, it enqueues it to the first workers queue.

  At least one worker thread is necessary.
*/
#define WORKER_COUNT 1

/* An request contains the message received from the port set.  */
struct request
{
  struct item item;
  mig_routine_t routine;
  mach_msg_header_t *inp;
  mach_msg_header_t *outp;
};

/* A worker.  */
struct worker
{
  struct requests *requests;	/* our pagers request queue */
  struct queue queue;	/* other workers may delegate requests to us */
  unsigned long tag;	/* tag of the object we are working on */
};

/* This is the queue for incoming requests.  A single thread receives
   messages from the port set, looks the service routine up, and
   enqueues the request here.  */
struct requests
{
  struct port_bucket *bucket;
  struct queue queue;
  int asleep;
  pthread_cond_t wakeup;
  pthread_mutex_t lock;
  struct worker workers[WORKER_COUNT];
};

/* Demultiplex a single message directed at a pager port; INP is the
   message received; fill OUTP with the reply.  */
static int
pager_demuxer (struct requests *requests,
	       mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  error_t err = MIG_NO_REPLY;

  /* The maximum size of the reply is 2048 bytes.  See the MIG source
     for details.  */
  const mach_msg_size_t max_size = 2048;

  mig_routine_t routine;
  if (! ((routine = _pager_seqnos_memory_object_server_routine (inp)) ||
	 (routine = _pager_seqnos_notify_server_routine (inp))))
    return FALSE;

#define MASK	(8u - 1u)
  mach_msg_size_t padded_size = (inp->msgh_size + MASK) & ~MASK;
#undef MASK

  struct request *r = malloc (sizeof *r + padded_size + max_size);
  if (r == NULL)
    {
      err = ENOMEM;
      goto out;
    }

  r->routine = routine;
  r->inp = (mach_msg_header_t *) ((char *) r + sizeof *r);
  memcpy (r->inp, inp, inp->msgh_size);

  r->outp = (mach_msg_header_t *) ((char *) r + sizeof *r + padded_size);
  memcpy (r->outp, outp, sizeof *outp);

  pthread_mutex_lock (&requests->lock);

  queue_enqueue (&requests->queue, &r->item);

  /* Awake worker.  */
  if (requests->asleep > 0)
      pthread_cond_signal (&requests->wakeup);

  pthread_mutex_unlock (&requests->lock);

  /* A worker thread will reply.  */
  err = MIG_NO_REPLY;

 out:
  ((mig_reply_header_t *) outp)->RetCode = err;
  return TRUE;
}

/* Consumes requests from the queue.  */
static void *
worker_func (void *arg)
{
  struct worker *self = (struct worker *) arg;
  struct requests *requests = self->requests;
  struct request *r = NULL;

  while (1)
    {
      int i;
      mach_msg_return_t mr;

      /* Free previous message.  */
      free (r);

      pthread_mutex_lock (&requests->lock);

      /* First, look in our queue for more requests to the object we
	 have been working on lately.  Some other thread might have
	 delegated them to us.  */
      r = queue_dequeue (&self->queue);
      if (r != NULL)
	goto got_one;

      /* Nope.  Clear our tag and...  */
      self->tag = 0;

    get_request_locked:
      /* ... get a request from the global queue instead.  */
      while ((r = queue_dequeue (&requests->queue)) == NULL)
	{
	  requests->asleep += 1;
	  pthread_cond_wait (&requests->wakeup, &requests->lock);
	  requests->asleep -= 1;
	}

      for (i = 0; i < WORKER_COUNT; i++)
	if (requests->workers[i].tag
	    == (unsigned long) r->inp->msgh_local_port)
	  {
	    /* Some other thread is working on that object.  Delegate
	       the request to that worker.  */
	    queue_enqueue (&requests->workers[i].queue, &r->item);
	    goto get_request_locked;
	  }

      /* Claim responsibility for this object by setting our tag.  */
      self->tag = (unsigned long) r->inp->msgh_local_port;

    got_one:
      pthread_mutex_unlock (&requests->lock);

      /* Call the server routine.  */
      (*r->routine) (r->inp, r->outp);

      /* What follows is basically the second part of
	 mach_msg_server_timeout.  */
      mig_reply_header_t *request = (mig_reply_header_t *) r->inp;
      mig_reply_header_t *reply = (mig_reply_header_t *) r->outp;

      switch (reply->RetCode)
	{
	case KERN_SUCCESS:
	  /* Hunky dory.  */
	  break;

	case MIG_NO_REPLY:
	  /* The server function wanted no reply sent.
	     Loop for another request.	*/
	  continue;

	default:
	  /* Some error; destroy the request message to release any
	     port rights or VM it holds.  Don't destroy the reply port
	     right, so we can send an error message.  */
	  request->Head.msgh_remote_port = MACH_PORT_NULL;
	  mach_msg_destroy (&request->Head);
	  break;
	}

      if (reply->Head.msgh_remote_port == MACH_PORT_NULL)
	{
	  /* No reply port, so destroy the reply.  */
	  if (reply->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX)
	    mach_msg_destroy (&reply->Head);
	  continue;
	}

      /* Send the reply.  */
      mr = mach_msg (&reply->Head,
		     MACH_SEND_MSG,
		     reply->Head.msgh_size,
		     0,
		     MACH_PORT_NULL,
		     0,
		     MACH_PORT_NULL);

      switch (mr)
	{
	case MACH_SEND_INVALID_DEST:
	  /* The reply can't be delivered, so destroy it.  This error
	     indicates only that the requester went away, so we
	     continue and get the next request.  */
	  mach_msg_destroy (&reply->Head);
	  break;

	default:
	  /* Some other form of lossage; there is not much we can
	     do here.  */
	  error (0, mr, "mach_msg");
	}
    }

  /* Not reached.  */
  return NULL;
}

/* A top-level function for the paging thread that just services paging
   requests.  */
static void *
service_paging_requests (void *arg)
{
  struct requests *requests = arg;

  int demuxer (mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
  {
    return pager_demuxer (requests, inp, outp);
  }

  ports_manage_port_operations_one_thread (requests->bucket,
					   demuxer,
					   0);
  /* Not reached.  */
  return NULL;
}

/* Start the worker threads libpager uses to service requests.  */
error_t
pager_start_workers (struct port_bucket *pager_bucket)
{
  error_t err;
  int i;
  pthread_t t;
  struct requests *requests;

  requests = malloc (sizeof *requests);
  if (requests == NULL)
    return ENOMEM;

  requests->bucket = pager_bucket;
  requests->asleep = 0;
  queue_init (&requests->queue);
  pthread_cond_init (&requests->wakeup, NULL);
  pthread_mutex_init (&requests->lock, NULL);

  /* Make a thread to service paging requests.  */
  err = pthread_create (&t, NULL, service_paging_requests, requests);
  if (err)
    return err;
  pthread_detach (t);

  for (i = 0; i < WORKER_COUNT; i++)
    {
      requests->workers[i].requests = requests;
      requests->workers[i].tag = 0;
      queue_init (&requests->workers[i].queue);

      err = pthread_create (&t, NULL, &worker_func, &requests->workers[i]);
      if (err)
	return err;
      pthread_detach (t);
    }

  return err;
}
