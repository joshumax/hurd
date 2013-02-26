/* Connection queues

   Copyright (C) 1995, 2012 Free Software Foundation, Inc.

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

#ifndef __CONNQ_H__
#define __CONNQ_H__

#include <errno.h>

/* Forward.  */
struct connq;
struct sock;

/* Create a new listening queue, returning it in CQ.  The resulting queue
   will be of zero length, that is it won't allow connections unless someone
   is already listening (change this with connq_set_length).  */
error_t connq_create (struct connq **cq);

/* Destroy a queue.  */
void connq_destroy (struct connq *cq);

/* Return a connection request on CQ.  If SOCK is NULL, the request is
   left in the queue.  If TIMEOUT denotes a value of 0, EWOULDBLOCK is
   returned when there are no immediate connections available.
   Otherwise this value is used to limit the wait duration.  If TIMEOUT
   is NULL, the wait duration isn't bounded.  */
error_t connq_listen (struct connq *cq, struct timespec *tsp,
		      struct sock **sock);

/* Try to connect SOCK with the socket listening on CQ.  If NOBLOCK is
   true, then return EWOULDBLOCK if there are no connections
   immediately available.  On success, this call must be followed up
   either connq_connect_complete or connq_connect_cancel.  */
error_t connq_connect (struct connq *cq, int noblock);

/* Follow up to connq_connect.  Completes the connection, SOCK is the
   new server socket.  */
void connq_connect_complete (struct connq *cq, struct sock *sock);

/* Follow up to connq_connect.  Cancel the connect.  */
void connq_connect_cancel (struct connq *cq);

/* Set CQ's queue length to LENGTH.  Any sockets already waiting for a
   connections that are past the new length remain.  */
error_t connq_set_length (struct connq *cq, int length);

#endif /* __CONNQ_H__ */
