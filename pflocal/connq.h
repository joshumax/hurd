/* Connection queues

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

#ifndef __CONNQ_H__
#define __CONNQ_H__

#include <errno.h>

/* Unknown types */
struct connq;
struct connq_request;
struct sock;

/* Create a new listening queue, returning it in CQ.  The resulting queue
   will be of zero length, that is it won't allow connections unless someone
   is already listening (change this with connq_set_length).  */
error_t connq_create (struct connq **cq);

/* Destroy a queue.  */
void connq_destroy (struct connq *cq);

/* Wait for a connection attempt to be made on CQ, and return the connecting
   socket in SOCK, and a request tag in REQ.  If REQ is NULL, the request is
   left in the queue, otherwise connq_request_complete must be called on REQ
   to allow the requesting thread to continue.  If NOBLOCK is true,
   EWOULDBLOCK is returned when there are no immediate connections
   available.  CQ should be unlocked.  */
error_t connq_listen (struct connq *cq, int noblock,
		      struct connq_request **req, struct sock **sock);

/* Return the error code ERR to the thread that made the listen request REQ,
   returned from a previous connq_listen.  */
void connq_request_complete (struct connq_request *req, error_t err);

/* Set CQ's queue length to LENGTH.  Any sockets already waiting for a
   connections that are past the new length will fail with ECONNREFUSED.  */
error_t connq_set_length (struct connq *cq, int length);

/* Try to connect SOCK with the socket listening on CQ.  If NOBLOCK is true,
   then return EWOULDBLOCK immediately when there are no immediate
   connections available. Neither SOCK nor CQ should be locked.  */
error_t connq_connect (struct connq *cq, int noblock, struct sock *sock);

#endif /* __CONNQ_H__ */
