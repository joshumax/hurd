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

#include <assert-backtrace.h>
#include <error.h>
#include <mach/mig_errors.h>
#include <pthread.h>
#include <string.h>

#include "priv.h"
#include "memory_object_S.h"
#include "libports/notify_S.h"
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
};

/* A struct request object is immediately followed by the received
   message.  */
static inline mach_msg_header_t *
request_inp (const struct request *r)
{
  return (mach_msg_header_t *) ((char *) r + sizeof *r);
}

/* A worker.  */
struct worker
{
  struct pager_requests *requests;	/* our pagers request queue */
  struct queue queue;	/* other workers may delegate requests to us */
  unsigned long tag;	/* tag of the object we are working on */
};

/* This is the queue for incoming requests.  A single thread receives
   messages from the port set, looks the service routine up, and
   enqueues the request here.  */
struct pager_requests
{
  struct port_bucket *bucket;
  /* Normally, both queues are the same.  However, when the workers are
     inhibited, a new queue_in is created, but queue_out is left as the
     old value, so the workers drain queue_out but do not receive new
     requests.  */
  struct queue *queue_in;	/* the queue to add to */
  struct queue *queue_out;	/* the queue to take from */
  int asleep;
  pthread_cond_t wakeup;
  pthread_cond_t inhibit_wakeup;
  pthread_mutex_t lock;
  struct worker workers[WORKER_COUNT];
};

/* Demultiplex a single message directed at a pager port; INP is the
   message received; fill OUTP with the reply.  */
static int
pager_demuxer (struct pager_requests *requests,
	       mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  error_t err = MIG_NO_REPLY;

  mig_routine_t routine;
  if (! ((routine = _pager_memory_object_server_routine (inp)) ||
	 (routine = ports_notify_server_routine (inp))))
    return FALSE;

#define MASK	(8u - 1u)
  mach_msg_size_t padded_size = (inp->msgh_size + MASK) & ~MASK;
#undef MASK

  struct request *r = malloc (sizeof *r + padded_size);
  if (r == NULL)
    {
      err = ENOMEM;
      goto out;
    }

  r->routine = routine;
  memcpy (request_inp (r), inp, inp->msgh_size);

  pthread_mutex_lock (&requests->lock);

  queue_enqueue (requests->queue_in, &r->item);

  /* Awake worker, but only if not inhibited.  */
  if (requests->asleep > 0 && requests->queue_in == requests->queue_out)
      pthread_cond_signal (&requests->wakeup);

  pthread_mutex_unlock (&requests->lock);

  /* A worker thread will reply.  */
  err = MIG_NO_REPLY;

 out:
  ((mig_reply_header_t *) outp)->RetCode = err;
  return TRUE;
}

/* XXX: The libc should provide this function.  */
static void
mig_reply_setup (
	const mach_msg_header_t	*in,
	mach_msg_header_t	*out)
{
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

#define	InP	(in)
#define	OutP	((mig_reply_header_t *) out)
      OutP->Head.msgh_bits =
	MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(InP->msgh_bits), 0);
      OutP->Head.msgh_size = sizeof *OutP;
      OutP->Head.msgh_remote_port = InP->msgh_remote_port;
      OutP->Head.msgh_local_port = MACH_PORT_NULL;
      OutP->Head.msgh_seqno = 0;
      OutP->Head.msgh_id = InP->msgh_id + 100;
      OutP->RetCodeType = RetCodeType;
      OutP->RetCode = MIG_BAD_ID;
#undef InP
#undef OutP
}

/* Consumes requests from the queue.  */
static void *
worker_func (void *arg)
{
  struct worker *self = (struct worker *) arg;
  struct pager_requests *requests = self->requests;
  struct request *r = NULL;
  mig_reply_header_t reply_msg;

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
      while ((r = queue_dequeue (requests->queue_out)) == NULL)
	{
	  requests->asleep += 1;
	  if (requests->asleep == WORKER_COUNT)
	    pthread_cond_broadcast (&requests->inhibit_wakeup);
	  pthread_cond_wait (&requests->wakeup, &requests->lock);
	  requests->asleep -= 1;
	}

      for (i = 0; i < WORKER_COUNT; i++)
	if (requests->workers[i].tag
	    == (unsigned long) request_inp (r)->msgh_local_port)
	  {
	    /* Some other thread is working on that object.  Delegate
	       the request to that worker.  */
	    queue_enqueue (&requests->workers[i].queue, &r->item);
	    goto get_request_locked;
	  }

      /* Claim responsibility for this object by setting our tag.  */
      self->tag = (unsigned long) request_inp (r)->msgh_local_port;

    got_one:
      pthread_mutex_unlock (&requests->lock);

      mig_reply_setup (request_inp (r), (mach_msg_header_t *) &reply_msg);

      /* Call the server routine.  */
      (*r->routine) (request_inp (r), (mach_msg_header_t *) &reply_msg);

      /* What follows is basically the second part of
	 mach_msg_server_timeout.  */
      mig_reply_header_t *request = (mig_reply_header_t *) request_inp (r);
      mig_reply_header_t *reply = &reply_msg;

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
  struct pager_requests *requests = arg;

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
pager_start_workers (struct port_bucket *pager_bucket,
		     struct pager_requests **out_requests)
{
  error_t err;
  int i;
  pthread_t t;
  struct pager_requests *requests;

  assert_backtrace (out_requests != NULL);

  requests = malloc (sizeof *requests);
  if (requests == NULL)
    {
      err = ENOMEM;
      goto done;
    }

  requests->bucket = pager_bucket;
  requests->asleep = 0;

  requests->queue_in = malloc (sizeof *requests->queue_in);
  if (requests->queue_in == NULL)
    {
      err = ENOMEM;
      goto done;
    }
  queue_init (requests->queue_in);
  /* Until the workers are inhibited, both queues are the same.  */
  requests->queue_out = requests->queue_in;

  pthread_cond_init (&requests->wakeup, NULL);
  pthread_cond_init (&requests->inhibit_wakeup, NULL);
  pthread_mutex_init (&requests->lock, NULL);

  /* Make a thread to service paging requests.  */
  err = pthread_create (&t, NULL, service_paging_requests, requests);
  if (err)
    goto done;
  pthread_detach (t);

  for (i = 0; i < WORKER_COUNT; i++)
    {
      requests->workers[i].requests = requests;
      requests->workers[i].tag = 0;
      queue_init (&requests->workers[i].queue);

      err = pthread_create (&t, NULL, &worker_func, &requests->workers[i]);
      if (err)
	goto done;
      pthread_detach (t);
    }

done:
  if (err)
    *out_requests = NULL;
  else
    *out_requests = requests;

  return err;
}

error_t
pager_inhibit_workers (struct pager_requests *requests)
{
  error_t err = 0;

  pthread_mutex_lock (&requests->lock);

  /* Check the workers are not already inhibited.  */
  assert_backtrace (requests->queue_out == requests->queue_in);

  /* Any new paging requests will go into a new queue.  */
  struct queue *new_queue = malloc (sizeof *new_queue);
  if (new_queue == NULL)
    {
      err = ENOMEM;
      goto done_locked;
    }
  queue_init (new_queue);
  requests->queue_in = new_queue;

  /* Wait until all the workers are asleep and the queue has been
     drained. All individual worker queues must have been drained, as
     they are populated while the relevant worker is still running, and
     it will always drain its personal queue before sleeping.
     Check that the queue is empty, since it's possible that a request
     came in, was queued and a worker was signalled but the lock was
     acquired here before the worker woke up.  */
  while (requests->asleep < WORKER_COUNT || !queue_empty(requests->queue_out))
    pthread_cond_wait (&requests->inhibit_wakeup, &requests->lock);

done_locked:
  pthread_mutex_unlock (&requests->lock);
  return err;
}

void
pager_resume_workers (struct pager_requests *requests)
{
  pthread_mutex_lock (&requests->lock);

  /* Check the workers are inhibited.  */
  assert_backtrace (requests->queue_out != requests->queue_in);
  assert_backtrace (requests->asleep == WORKER_COUNT);
  assert_backtrace (queue_empty(requests->queue_out));

  /* The queue has been drained and will no longer be used.  */
  free (requests->queue_out);
  requests->queue_out = requests->queue_in;

  /* We need to wake up all workers, as there could be multiple requests
     in the new queue.  */
  pthread_cond_broadcast (&requests->wakeup);

  pthread_mutex_unlock (&requests->lock);
}
