/* Listen queue functions

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <socket.h>

#include "pflocal.h"

/* A queue for queueing incoming connections.  */
struct connq
{
  /* True if all connection requests should be treated as non-blocking.  */
  int noqueue;

  /* The connection request queue.  */
  unsigned length;
  unsigned head, tail;
  struct connq_request *queue;

  /* Threads that have done an accept on this queue wait on this condition.  */
  struct condition listeners;
  unsigned num_listeners;
  struct mutex lock;
}

extern inline qnext (struct connq *ca, unsigned pos)
{
  return (pos + 1 == cq->length ? 0 : pos + 1);
}

extern inline qprev (struct connq *ca, unsigned pos)
{
  return (pos == 0 ? cq->length - 1 : pos - 1);
}

/* ---------------------------------------------------------------- */

/* A data block allocated by a thread waiting on a connq, which is used to
   get information from and to the thread.  */
struct connq_request
{
  /* The socket that's waiting to connect.  */
  struct sock *sock;

  /* What the waiting thread blocks on.  */
  struct condition signal;
  struct mutex lock;

  /* Set to true when this request has been dealt with, to guard against
     spurious conditions being signaled.  */
  int completed;

  /* After the waiting thread is unblocked, this is the result, either 0 if
     SOCK has been connected, or an error.  */
  error_t err;
};

static inline void
connq_request_init (struct sock *sock, struct connq_request *req)
{
  req->err = 0;
  req->sock = sock;
  req->completed = 0;
  condition_init (&req->signal);
  mutex_init (&req->lock);
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
    return ENOMEM;

  new->noqueue = 1;		/* By default, don't queue requests.  */
  new->length = 0;
  new->head = new->tail = 0;
  new->queue = NULL;
  new->num_listeners = 0;

  mutex_init (&new->lock);
  condition_init (&new->listeners);

  *cq = new;
  return 0;
}

/* ---------------------------------------------------------------- */

/* Wait for a connection attempt to be made on CQ, and return the connecting
   socket in SOCK, and a request tag in REQ.  If REQ is NULL, the request is
   left in the queue, otherwise connq_request_complete must be called on REQ
   to allow the requesting thread to continue.  If NOBLOCK is true,
   EWOULDBLOCK is returned when there are no immediate connections
   available. */
error_t 
connq_listen (struct connq *cq, int noblock,
	      struct connq_request **req, struct sock **sock)
{
  error_t err = 0;

  mutex_lock (&cq->lock);

  if (noblock && cq->head == cq->tail)
    return EWOULDBLOCK;

  cq->num_listeners++;

  while (cq->head == cq->tail)
    condition_wait (&cq->listeners, &cq->lock);

  if (req != NULL)
    /* Dequeue the next request, if desired.  */
    {
      *req = cq->queue[cq->tail];
      cq->tail = qnext (cq, cq->tail);
      mutex_lock (&(*req)->lock);
      if (sock != NULL)
	*sock = (*req)->sock;
    }

  cq->num_listeners--;

  mutex_unlock (&cq->lock);

  return 0;    
}

/* Return the error code ERR to the thread that made the listen request REQ,
   returned from a previous connq_listen.  */
void
connq_request_complete (struct connq_request *req, error_t err)
{
  req->err = err;
  req->complete = 1;
  condition_signal (&req->signal, &req->lock);
}

/* Try to connect SOCK with the socket listening on CQ.  If NOBLOCK is true,
   then return EWOULDBLOCK immediately when there are no immediate
   connections available. */
error_t
connq_connect (struct connq *cq, int noblock, struct sock *sock)
{
  error_t err = 0;
  struct connq_request req;
  unsigned next;

  mutex_lock (&cq->lock);

  if ((noblock || cq->noqueue) && cq->num_listeners == 0)
    return EWOULDBLOCK;

  next = qnext (cq, cq->head);
  if (next == cq->tail)
    err = ECONNREFUSED;
  else
    {
      cq->queue[cq->head] = &req;
      cq->head = next;
    }
    
  /* Hold REQ.LOCK before we signal the condition so that we're sure to be
     woken up.  */
  mutex_lock (&req.lock);
  condition_signal (&cq->listeners, &cq->lock);

  while (!req.completed)
    condition_wait (&req.signal, &req.lock);

  return req.err;
}

/* Set CQ's queue length to LENGTH.  Any sockets already waiting for a
   connections that are past the new length will fail with ECONNREFUSED.  */
error_t 
connq_set_length (struct connq *cq, int length)
{
  mutex_lock (&cq->lock);

  if (length > cq->length)
    /* Growing the queue is simple... */
    cq->queue = realloc (cq->queue, sizeof (struct connq_request *) * length);
  else
    /* Shrinking it less so.  */
    {
      int i;
      struct connq_request *new_queue =
	malloc (sizeof (struct connq_request *) * length);

      for (i = 0; i < cq->length && cq->head != cq->tail;)
	{
	  cq->head = qprev (cq, cq->head)
	    if (i < length)
	      /* Keep this connect request in the queue.  */
	      new_queue[length - i] = cq->queue[cq->head];
	    else
	      /* Punt this one.  */
	      connq_request_complete (cq->queue[cq->head], ECONNREFUSED);
	}
    }

  cq->noqueue = 0;		/* Turn on queueing.  */

  mutex_unlock (&cq->lock);
}
