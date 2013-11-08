/* Handle interruping rpcs because of notification

   Copyright (C) 1995, 2001 Free Software Foundation, Inc.

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

#include "ports.h"

/* A linked list of ports for which notification has been requested.  */
struct ports_notify *_ports_notifications;

/* Free lists for notify structures.  */
struct ports_notify *_ports_free_ports_notifies;
struct rpc_notify *_ports_free_rpc_notifies;

/* Interrupt any rpcs on OBJECT that have requested such.  */
void
ports_interrupt_notified_rpcs (void *object,
			       mach_port_t port, mach_msg_id_t what)
{
  if (_ports_notifications)
    {
      struct ports_notify *np;

      pthread_mutex_lock (&_ports_lock);
      for (np = _ports_notifications; np; np = np->next)
	if (np->port == port && np->what == what)
	  {
	    struct rpc_notify *req;
	    for (req = np->reqs; req; req = req->next_req)
	      if (req->pending)
		{
		  req->pending--;
		  hurd_thread_cancel (req->rpc->thread);
		}
	    break;
	  }
      pthread_mutex_unlock (&_ports_lock);
    }
}

static void
remove_req (struct rpc_notify *req)
{
  struct ports_notify *np = req->notify;

  /* Take REQ out of the list of notified rpcs.  */
  if (req->next_req)
    req->next_req->prev_req_p = req->prev_req_p;
  *req->prev_req_p = req->next_req;

  if (np->reqs == 0)
    /* Now NP has no more reqests, so we can free it too.  */
    {
      /* Take NP out of the active list... */
      if (np->next)
	np->next->prevp = np->prevp;
      *np->prevp = np->next;

      /* And put it on the free list.  */
      np->next = _ports_free_ports_notifies;
      _ports_free_ports_notifies = np;

      if (np->pending)
	/* And cancel the associated notification.  */
	{
	  mach_port_t old;
	  error_t err =
	    mach_port_request_notification (mach_task_self (), np->port,
					    np->what, 0, MACH_PORT_NULL,
					    MACH_MSG_TYPE_MAKE_SEND_ONCE,
					    &old);
	  if (! err && old != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), old);
	}	
    }
}

/* Remove RPC from the list of notified rpcs, cancelling any pending
   notifications.  _PORTS_LOCK should be held.  */
void
_ports_remove_notified_rpc (struct rpc_info *rpc)
{
  struct rpc_notify *req = rpc->notifies;

  if (req)
    /* Cancel RPC's notify requests.  */
    {
      struct rpc_notify *last = req;

      while (last->next)
	{
	  remove_req (last);
	  last = last->next;
	}
      remove_req (last);

      /* Put the whole chain on the free list.  */
      last->next = _ports_free_rpc_notifies;
      _ports_free_rpc_notifies = req;
    }
}
