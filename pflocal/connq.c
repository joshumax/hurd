/* Listen queue functions

   Copyright (C) 1995,96,2001,2012 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <pthread.h>
#include <assert-backtrace.h>
#include <stdlib.h>

#include "connq.h"

/* A queue for queueing incoming connections.  */
struct connq
{
  /* The connection request queue.  */
  struct connq_request *head;
  struct connq_request **tail;
  unsigned count;
  unsigned max;

  /* Threads that have done an accept on this queue wait on this condition.  */
  pthread_cond_t listeners;
  unsigned num_listeners;

  /* Threads that have done a connect on this queue wait on this condition.  */
  pthread_cond_t connectors;
  unsigned num_connectors;

  pthread_mutex_t lock;
};

/* ---------------------------------------------------------------- */

/* A data block allocated by a thread waiting on a connq, which is used to
   get information from and to the thread.  */
struct connq_request
{
  struct connq_request *next;

  /* The socket that's waiting to connect.  */
  struct sock *sock;
};

static inline void
connq_request_init (struct connq_request *req, struct sock *sock)
{
  req->sock = sock;
}

/* Enqueue connection request REQ onto CQ.  CQ must be locked.  */
static void
connq_request_enqueue (struct connq *cq, struct connq_request *req)
{
  assert_backtrace (pthread_mutex_trylock (&cq->lock));

  req->next = NULL;
  *cq->tail = req;
  cq->tail = &req->next;

  cq->count ++;
}

/* Dequeue a pending request from CQ.  CQ must be locked and must not
   be empty.  */
static struct connq_request *
connq_request_dequeue (struct connq *cq)
{
  struct connq_request *req;

  assert_backtrace (pthread_mutex_trylock (&cq->lock));
  assert_backtrace (cq->head);

  req = cq->head;
  cq->head = req->next;
  if (! cq->head)
    /* We just dequeued the last element.  Fixup the tail pointer.  */
    cq->tail = &cq->head;

  cq->count --;

  return req;
}

/* ---------------------------------------------------------------- */

/* Create a new listening queue, returning it in CQ.  The resulting queue
   will be of zero length, that is it won't allow connections unless someone
   is already listening (change this with connq_set_length).  */
error_t
connq_create (struct connq **cq)
{
  struct connq *new = malloc (sizeof (struct connq));

  if (!new)
    return ENOBUFS;

  new->head = NULL;
  new->tail = &new->head;
  new->count = 0;
  /* By default, don't queue requests.  */
  new->max = 0;

  new->num_listeners = 0;
  new->num_connectors = 0;

  pthread_mutex_init (&new->lock, NULL);
  pthread_cond_init (&new->listeners, NULL);
  pthread_cond_init (&new->connectors, NULL);

  *cq = new;
  return 0;
}

/* Destroy a queue.  */
void
connq_destroy (struct connq *cq)
{
  /* Everybody in the queue should hold a reference to the socket
     containing the queue.  */
  assert_backtrace (! cq->head);
  assert_backtrace (cq->count == 0);

  free (cq);
}

/* ---------------------------------------------------------------- */

/* Return a connection request on CQ.  If SOCK is NULL, the request is
   left in the queue.  If TIMEOUT denotes a value of 0, EWOULDBLOCK is
   returned when there are no immediate connections available.
   Otherwise this value is used to limit the wait duration.  If TIMEOUT
   is NULL, the wait duration isn't bounded.  */
error_t
connq_listen (struct connq *cq, struct timespec *tsp, struct sock **sock)
{
  error_t err = 0;

  pthread_mutex_lock (&cq->lock);

  if (tsp && tsp->tv_sec == 0 && tsp->tv_nsec == 0 && cq->count == 0
      && cq->num_connectors == 0)
    {
      pthread_mutex_unlock (&cq->lock);
      return EWOULDBLOCK;
    }

  if (! sock && (cq->count > 0 || cq->num_connectors > 0))
    /* The caller just wants to know if a connection ready.  */
    {
      pthread_mutex_unlock (&cq->lock);
      return 0;
    }

  cq->num_listeners++;

  if (cq->count == 0)
    /* The request queue is empty.  */
    {
      assert_backtrace (! cq->head);

      if (cq->num_connectors > 0)
	/* Someone is waiting for an acceptor.  Signal that we can
	   service their request.  */
	pthread_cond_signal (&cq->connectors);

      do
	{
	  err = pthread_hurd_cond_timedwait_np (&cq->listeners, &cq->lock, tsp);
	  if (err)
	  {
	    cq->num_listeners--;
	    goto out;
	  }
	}
      while (cq->count == 0);
    }

  assert_backtrace (cq->head);

  if (sock)
    /* Dequeue the next request, if desired.  */
    {
      struct connq_request *req = connq_request_dequeue (cq);
      *sock = req->sock;
      free (req);
    }
  else if (cq->num_listeners > 0)
    /* The caller will not actually process this request but someone
       else could.  (This case is rare but possible: it would require
       one thread to do a select on the socket and a second to do an
       accept.)  */
    pthread_cond_signal (&cq->listeners);
  else
    /* There is no one else to process the request and the connection
       has now been initiated.  This is not actually a problem as even
       if the current queue limit is 0, the connector will queue the
       request and another listener (should) eventually come along.
       (In fact it is very probably as the caller has likely done a
       select and will now follow up with an accept.)  */
    { /* Do nothing.  */ }

 out:
  pthread_mutex_unlock (&cq->lock);
  return err;
}

/* Try to connect SOCK with the socket listening on CQ.  If NOBLOCK is
   true, then return EWOULDBLOCK if there are no connections
   immediately available.  On success, this call must be followed up
   either connq_connect_complete or connq_connect_cancel.  */
error_t
connq_connect (struct connq *cq, int noblock)
{
  pthread_mutex_lock (&cq->lock);

  /* Check for listeners after we've locked CQ for good.  */

  if (noblock
      && cq->count + cq->num_connectors >= cq->max + cq->num_listeners)
    /* We are in non-blocking mode and would have to wait to secure an
       entry in the listen queue.  */
    {
      pthread_mutex_unlock (&cq->lock);
      return EWOULDBLOCK;
    }

  cq->num_connectors ++;

  while (cq->count + cq->num_connectors > cq->max + cq->num_listeners)
    /* The queue is full and there is no immediate listener to service
       us.  Block until we can get a slot.  */
    if (pthread_hurd_cond_wait_np (&cq->connectors, &cq->lock))
      {
	cq->num_connectors --;
	pthread_mutex_unlock (&cq->lock);
	return EINTR;
      }

  pthread_mutex_unlock (&cq->lock);

  return 0;
}

/* Follow up to connq_connect.  Completes the connect, SOCK is the new
   server socket.  */
void
connq_connect_complete (struct connq *cq, struct sock *sock)
{
  struct connq_request *req;

  req = malloc (sizeof (struct connq_request));
  if (! req)
    abort ();

  connq_request_init (req, sock);

  pthread_mutex_lock (&cq->lock);

  assert_backtrace (cq->num_connectors > 0);
  cq->num_connectors --;

  connq_request_enqueue (cq, req);

  if (cq->num_listeners > 0)
    /* Wake a listener up.  We must consume the listener ref here as
       someone else might call this function before the listener
       thread dequeues this request.  */
    {
      cq->num_listeners --;
      pthread_cond_signal (&cq->listeners);
    }

  pthread_mutex_unlock (&cq->lock);
}

/* Follow up to connq_connect.  Cancel the connect.  */
void
connq_connect_cancel (struct connq *cq)
{
  pthread_mutex_lock (&cq->lock);

  assert_backtrace (cq->num_connectors > 0);
  cq->num_connectors --;

  if (cq->count + cq->num_connectors >= cq->max + cq->num_listeners)
    /* A connector is blocked and could use the spot we reserved.  */
    pthread_cond_signal (&cq->connectors);

  pthread_mutex_unlock (&cq->lock);
}

/* Set CQ's queue length to LENGTH.  */
error_t
connq_set_length (struct connq *cq, int max)
{
  int omax;

  pthread_mutex_lock (&cq->lock);
  omax = cq->max;
  cq->max = max;

  if (max > omax && cq->count >= omax && cq->count < max
      && cq->num_connectors >= cq->num_listeners)
    /* This is an increase in the number of connection slots which has
       made some slots available and there are waiting threads.  Wake
       them up.  */
    pthread_cond_broadcast (&cq->listeners);

  pthread_mutex_unlock (&cq->lock);

  return 0;
}
