/* Mark an rpc to be interrupted when a port dies

   Copyright (C) 1995, 96, 99 Free Software Foundation, Inc.

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
#include <assert-backtrace.h>

/* Arrange for hurd_cancel to be called on RPC's thread if OBJECT gets notified
   that any of the things in COND have happened to PORT.  RPC should be an
   rpc on OBJECT.  */
error_t
ports_interrupt_rpc_on_notification (void *object,
				     struct rpc_info *rpc,
				     mach_port_t port, mach_msg_id_t what)
{
  int req_notify;
  struct ports_notify *pn;
  struct rpc_notify *new_req, *req;
  struct port_info *pi = object;

  pthread_mutex_lock (&_ports_lock);

  if (! MACH_PORT_VALID (port))
    /* PORT is already dead or bogus, so interrupt the rpc immediately.  */
    {
      hurd_thread_cancel (rpc->thread);
      pthread_mutex_unlock (&_ports_lock);
      return 0;
    }

  new_req = _ports_free_rpc_notifies;
  if (new_req)
    /* We got a req off the free list.  */
    _ports_free_rpc_notifies = new_req->next;
  else
    /* No free notify structs, allocate one; it's expected that 99% of the
       time we'll add a new structure, so we malloc while we don't have the
       lock, and free it if we're wrong.  */
    {
      pthread_mutex_unlock (&_ports_lock); /* Don't hold the lock during malloc. */
      new_req = malloc (sizeof (struct rpc_notify));
      if (! new_req)
	return ENOMEM;
      pthread_mutex_lock (&_ports_lock);
    }

  /* Find any existing entry for PORT/WHAT.  */
  for (pn = _ports_notifications; pn; pn = pn->next)
    if (pn->port == port && pn->what == what)
      break;

  if (! pn)
    /* A notification on a new port.  */
    {
      pn = _ports_free_ports_notifies;

      if (pn)
	_ports_free_ports_notifies = pn->next;
      else
	{
	  pn = malloc (sizeof (struct ports_notify));
	  if (! pn)
	    /* sigh.  Free what we've alloced and return.  */
	    {
	      new_req->next = _ports_free_rpc_notifies;
	      _ports_free_rpc_notifies = new_req;
	      pthread_mutex_unlock (&_ports_lock);
	      return ENOMEM;
	    }
	}

      pn->reqs = 0;
      pn->port = port;
      pn->what = what;
      pn->pending = 0;
      pthread_mutex_init (&pn->lock, NULL);

      pn->next = _ports_notifications;
      pn->prevp = &_ports_notifications;
      if (_ports_notifications)
	_ports_notifications->prevp = &pn->next;
      _ports_notifications = pn;
    }

  for (req = rpc->notifies; req; req = req->next)
    if (req->notify == pn)
      break;

  if (req)
    /* REQ is already pending for PORT/WHAT on RPC, so free NEW_REQ.  */
    {
      new_req->next = _ports_free_rpc_notifies;
      _ports_free_rpc_notifies = new_req;
    }
  else
    /* Add a new request for PORT/WHAT on RPC.  */
    {
      req = new_req;

      req->rpc = rpc;
      req->notify = pn;
      req->pending = 0;

      req->next_req = pn->reqs;
      req->prev_req_p = &pn->reqs;
      if (pn->reqs)
	pn->reqs->prev_req_p = &req->next_req;
      pn->reqs = req;

      req->next = rpc->notifies;
      rpc->notifies = req;
    }

  /* Make sure that this request results in an interrupt.  */
  req->pending++;

  /* Find out whether we should request a new notification (after we release
     _PORTS_LOCK) -- PN may be new, or left over after a previous
     notification (in which case our new request is likely to trigger an
     immediate notification).  */
  req_notify = !pn->pending;
  if (req_notify)
    pthread_mutex_lock (&pn->lock);

  pthread_mutex_unlock (&_ports_lock);

  if (req_notify)
    {
      mach_port_t old;
      error_t err =
	mach_port_request_notification (mach_task_self (), port,
					what, 1, pi->port_right,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);

      if (! err && old != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), old);

      pn->pending = 1;
      pthread_mutex_unlock (&pn->lock);

      return err;
    }
  else
    return 0;
}

/* Arrange for hurd_cancel to be called on the current thread, which should
   be an rpc on OBJECT, if PORT gets notified with the condition WHAT.  */
error_t
ports_interrupt_self_on_notification (void *object,
				      mach_port_t port, mach_msg_id_t what)
{
  struct rpc_info *rpc;
  struct port_info *pi = object;
  thread_t thread = hurd_thread_self ();

  pthread_mutex_lock (&_ports_lock);
  for (rpc = pi->current_rpcs; rpc; rpc = rpc->next)
    if (rpc->thread == thread)
      break;
  pthread_mutex_unlock (&_ports_lock);

  assert_backtrace (rpc);

  /* We don't have to worry about RPC going away after we dropped the lock
     because we're that thread, and we're still here.  */
  return ports_interrupt_rpc_on_notification (object, rpc, port, what);
}
