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
struct listenq
{
  /* True if all connection requests should be treated as non-blocking.  */
  int noqueue;

  /* The connection request queue.  */
  unsigned length;
  unsigned head, tail;
  struct listenq_request *queue;

  /* Threads that have done an accept on this queue wait on this condition.  */
  struct condition listeners;
  unsigned num_listeners;
  struct mutex lock;
}

/* ---------------------------------------------------------------- */

/* A data block allocated by a thread waiting on a listenq, which is used to
   get information from and to the thread.  */
struct listenq_request
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
listenq_request_init (struct sock *sock, struct listenq_request *lqr)
{
  lqr->err = 0;
  lqr->sock = sock;
  lqr->completed = 0;
  condition_init (&lqr->signal);
  mutex_init (&lqr->lock);
}

/* ---------------------------------------------------------------- */

/* Create a new listening queue, returning it in LQ.  The resulting queue
   will be of zero length, that is it won't allow connections unless someone
   is already listening (change this with listenq_set_length).  */
error_t
listenq_create (struct listenq **lq)
{
  struct listenq *new = malloc (sizeof (struct listenq));

  if (!new)
    return ENOMEM;

  new->noqueue = 1;		/* By default, don't queue requests.  */
  new->length = 0;
  new->head = new->tail = 0;
  new->queue = NULL;
  new->num_listeners = 0;

  mutex_init (&new->lock);
  condition_init (&new->listeners);

  *lq = new;
  return 0;
}

/* ---------------------------------------------------------------- */

/* Wait for a connection attempt to be made on LQ, and return the connecting
   socket in SOCK, and a request tag in REQ.  listenq_request_complete must be
   call on REQ to allow the requesting thread to continue.  If NOBLOCK is
   true, then return EWOULDBLOCK immediately when there are no immediate
   connections available.  */
error_t 
listenq_listen (struct listenq *lq, int noblock,
		struct sock **sock, struct listenq_request **req)
{
  error_t err = 0;

  mutex_lock (&lq->lock);

  if (noblock && lq->head == lq->tail)
    return EWOULDBLOCK;

  lq->num_listeners++;

  while (lq->head == lq->tail)
    condition_wait (&lq->listeners, &lq->lock);

  /* Dequeue the next request.  */
  *req = lq->queue[lq->tail];
  lq->tail = (lq->tail + 1 == lq->length ? 0 : lq->tail + 1);

  mutex_lock (&(*req)->lock);

  lq->num_listeners--;

  mutex_unlock (&lq->lock);

  *sock = (*req)->sock;
  return 0;    
}

/* Return the error code ERR to the thread that made the listen request REQ,
   returned from a previous listenq_listen.  */
void
listenq_request_complete (struct listenq_request *req, error_t err)
{
  req->err = err;
  req->complete = 1;
  condition_signal (&req->signal, &req->lock);
}

/* Try to connect SOCK with the socket listening on LQ.  If NOBLOCK is true,
   then return EWOULDBLOCK immediately when there are no immediate
   connections available. */
error_t
listenq_connect (struct listenq *lq, int noblock, struct sock *sock)
{
  error_t err = 0;
  struct listenq_request req;
  unsigned next;

  mutex_lock (&lq->lock);

  if ((noblock || lq->noqueue) && lq->num_listeners == 0)
    return EWOULDBLOCK;

  next = (lq->head + 1 == lq->length ? 0 : lq->head + 1);
  if (next == lq->tail)
    err = ECONNREFUSED;
  else
    {
      lq->queue[lq->head] = &req;
      lq->head = next;
    }
    
  /* Hold REQ.LOCK before we signal the condition so that we're sure to be
     woken up.  */
  mutex_lock (&req.lock);
  condition_signal (&lq->listeners, &lq->lock);

  while (!req.completed)
    condition_wait (&req.signal, &req.lock);

  return req.err;
}

/* Set LQ's queue length to LENGTH.  Any sockets already waiting for a
   connections that are past the new length will fail with ECONNREFUSED.  */
error_t 
listenq_set_length (struct listenq *lq, int length)
{
  int excess;

  mutex_lock (&lq->lock);

  lq->noqueue = 0;		/* Turn on queueing.  */

  /* Force any excess requests to fail.  */
  excess = lq->length - length;
  while (excess-- > 0)
    {
      assert (lq->head != lq->tail);
      lq->head = (lq->head == 0 ? lq->length - 1 : lq->head - 1);
      listenq_request_complete (lq->queue[lq->head], ECONNREFUSED);
    }

  /* ... */
}
