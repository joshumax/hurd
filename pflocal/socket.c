/* Socket-specific operations

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

#include <sys/socket.h>

#include <hurd/pipe.h>

#include "sock.h"
#include "connq.h"

#include "socket_S.h"

/* Connect two sockets */
error_t
S_socket_connect2 (struct sock_user *user1, struct sock_user *user2)
{
  error_t err;

  if (!user1 || !user2)
    return EOPNOTSUPP;

  err = sock_connect (user1->sock, user2->sock);

  /* Since USER2 isn't in the receiver position in the rpc, we get a send
     right for it (although we only use the receive right with the same
     name); be sure it's deallocated!  */
  mach_port_deallocate (mach_task_self (), user2->pi.port_right);

  return err;
}

/* Make sure we have a queue to listen on.  */
static error_t
ensure_connq (struct sock *sock)
{
  error_t err = 0;
debug (sock, "lock");
  mutex_lock (&sock->lock);
  if (!sock->listen_queue)
    err = connq_create (&sock->listen_queue);
debug (sock, "unlock");
  mutex_unlock (&sock->lock);
  return err;
}

/* Prepare a socket of appropriate type for future accept operations.  */
error_t
S_socket_listen (struct sock_user *user, int queue_limit)
{
  error_t err;
  if (!user)
    return EOPNOTSUPP;
  if (queue_limit < 0)
    return EINVAL;
  err = ensure_connq (user->sock);
  if (!err)
    err = connq_set_length (user->sock->listen_queue, queue_limit);
  return err;
}

error_t
S_socket_connect (struct sock_user *user, struct addr *addr)
{
  error_t err;
  struct sock *peer;

  if (! user)
    return EOPNOTSUPP;
  if (! addr)
    return ECONNREFUSED;

debug (user, "in");
  err = addr_get_sock (addr, &peer);
  if (!err)
    {
      struct sock *sock = user->sock;
      struct connq *cq = peer->listen_queue;
      if (cq)
	/* Only connect with sockets that are actually listening.  */
	{
debug (sock, "(sock) lock");
	  mutex_lock (&sock->lock);
	  if (sock->connect_queue)
	    /* SOCK is already doing a connect.  */
	    err = EALREADY;
	  else if (sock->flags & SOCK_CONNECTED)
	    /* SOCK_CONNECTED is only set for connection-oriented sockets,
	       which can only ever connect once.  [If we didn't do this test
	       here, it would eventually fail when it the listening socket
	       tried to accept our connection request.]  */
	    err = EISCONN;
	  else
	    {
	      /* Assert that we're trying to connect, so anyone else trying
	         to do so will fail with EALREADY.  */
debug (sock, "(sock) connect_queue: %p", cq);
	      sock->connect_queue = cq;
debug (sock, "(sock) unlock");
	      mutex_unlock (&sock->lock); /* Unlock SOCK while waiting.  */

debug (cq, "(cq) connect: %p", sock);
	      /* Try to connect.  */
	      err = connq_connect (cq, sock->flags & SOCK_NONBLOCK, sock);

	      /* We can safely set CONNECT_QUEUE to NULL, as no one else can
		 set it until we've done so.  */
debug (sock, "(sock) lock");
	      mutex_lock (&sock->lock);
debug (sock, "(sock) connect_queue: NULL");
	      sock->connect_queue = NULL;
	    }
debug (sock, "(sock) unlock");
	  mutex_unlock (&sock->lock);
	}
      else
	err = ECONNREFUSED;
      sock_deref (peer);
    }

debug (user, "out");
  return err;
}

/* Return a new connection from a socket previously listened.  */
error_t
S_socket_accept (struct sock_user *user,
		 mach_port_t *port, mach_msg_type_name_t *port_type,
		 mach_port_t *peer_addr_port,
		 mach_msg_type_name_t *peer_addr_port_type)
{
  error_t err;
  struct sock *sock;

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;

  err = ensure_connq (sock);
  if (!err)
    {
      struct connq_request *req;
      struct sock *peer_sock;

      err =
	connq_listen (sock->listen_queue, sock->flags & SOCK_NONBLOCK,
		      &req, &peer_sock);
      if (!err)
	{
	  struct sock *conn_sock;

	  err = sock_clone (sock, &conn_sock);
	  if (!err)
	    {
	      err = sock_connect (conn_sock, peer_sock);
	      if (!err)
		{
		  struct addr *peer_addr;
		  *port_type = MACH_MSG_TYPE_MAKE_SEND;
		  err = sock_create_port (conn_sock, port);
		  if (!err)
		    err = sock_get_addr (peer_sock, &peer_addr);
		  if (!err)
		    {
		      *peer_addr_port = ports_get_right (peer_addr);
		      *peer_addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
		      ports_port_deref (peer_addr);
		    }
		  else
		    /* TEAR DOWN THE CONNECTION XXX */;
		}
	      if (err)
		sock_free (conn_sock);
	    }

	  /* Communicate any error (or success) to the connecting thread.  */
	  connq_request_complete (req, err);
	}
    }

  return err;
}

/* Bind a socket to an address.  */
error_t
S_socket_bind (struct sock_user *user, struct addr *addr)
{
  if (! user)
    return EOPNOTSUPP;
  if (! addr)
    return EADDRNOTAVAIL;

  return sock_bind (user->sock, addr);
}

/* Shutdown a socket for reading or writing.  */
error_t
S_socket_shutdown (struct sock_user *user, int what)
{
  if (! user)
    return EOPNOTSUPP;
  sock_shutdown (user->sock,
		   (what != 1 ? SOCK_SHUTDOWN_READ : 0)
		 | (what != 0 ? SOCK_SHUTDOWN_WRITE : 0));
  return 0;
}

/* Find out the name of a socket.  */
error_t
S_socket_name (struct sock_user *user,
	       mach_port_t *addr_port, mach_msg_type_name_t *addr_port_type)
{
  error_t err;
  struct addr *addr;

  if (!user)
    return EOPNOTSUPP;

  err = sock_get_addr (user->sock, &addr);
  if (err)
    return err;

  *addr_port = ports_get_right (addr);
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;

  return 0;
}

/* Find out the name of the socket's peer.  */
error_t
S_socket_peername (struct sock_user *user,
		   mach_port_t *addr_port,
		   mach_msg_type_name_t *addr_port_type)
{
  return EOPNOTSUPP;		/* XXX */
  if (!user)
    return EOPNOTSUPP;
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
}

/* Send data over a socket, possibly including Mach ports.  */
error_t
S_socket_send (struct sock_user *user, struct addr *dest_addr, int flags,
	       char *data, size_t data_len,
	       mach_port_t *ports, size_t num_ports,
	       char *control, size_t control_len,
	       size_t *amount)
{
  error_t err;
  struct pipe *pipe;
  struct sock *dest_sock;
  struct addr *source_addr;

  if (!user || !dest_addr)
    return EOPNOTSUPP;

  if (flags & MSG_OOB)
    /* BSD local sockets don't support OOB data.  */
    return EOPNOTSUPP;

  err = addr_get_sock (dest_addr, &dest_sock);
  if (err)
    return err;

  if (user->sock->read_pipe->class != dest_sock->read_pipe->class)
    /* Sending to a different type of socket!  */
    err = EINVAL;		/* ? XXX */

  if (!err)
    err = sock_get_addr (user->sock, &source_addr);
  if (!err)
    {
      /* Grab the destination socket's read pipe directly, and stuff data
	 into it.  This is not quite the usage sock_acquire_read_pipe was
	 intended for, but it will work, as the only inappropiate errors
	 occur on a broken pipe, which shouldn't be possible with the sort of
	 sockets with which we can use socket_send...  XXXX */
      err = sock_acquire_read_pipe (dest_sock, &pipe);
      if (!err)
	{
	  err = pipe_send (pipe, source_addr, data, data_len,
			   control, control_len, ports, num_ports,
			   amount);
	  pipe_release_writer (pipe);
	  if (err)
	    /* The send failed, so free any resources it would have consumed
	       (mig gets rid of memory, but we have to do everything else). */
	    {
	      ports_port_deref (source_addr);
	      while (num_ports-- > 0)
		mach_port_deallocate (mach_task_self (), *ports++);
	    }
	}
    }

  sock_deref (dest_sock);

  return err;
}

/* Receive data from a socket, possibly including Mach ports.  */
error_t
S_socket_recv (struct sock_user *user,
	       mach_port_t *addr, mach_msg_type_name_t *addr_type,
	       int in_flags,
	       char **data, size_t *data_len,
	       mach_port_t **ports, mach_msg_type_name_t *ports_type,
	       size_t *num_ports,
	       char **control, size_t *control_len,
	       int *out_flags, size_t amount)
{
  error_t err;
  unsigned flags;
  struct pipe *pipe;
  void *source_addr = NULL;

  if (!user)
    return EOPNOTSUPP;

  if (flags & MSG_OOB)
    /* BSD local sockets don't support OOB data.  */
    return EOPNOTSUPP;

  flags =
    0;

  err = sock_acquire_read_pipe (user->sock, &pipe);
  if (err == EPIPE)
    /* EOF */
    {
      *data_len = 0;
      if (num_ports)
	*num_ports = 0;
      if (control_len)
	*control_len = 0;
    }
  else if (!err)
    {
      err =
	pipe_recv (pipe, user->sock->flags & SOCK_NONBLOCK, &flags,
		   &source_addr, data, data_len, amount,
		   control, control_len, ports, num_ports);
      pipe_release_reader (pipe);
    }

  if (!err)
    /* Setup mach ports for return.  */
    {
      *addr_type = MACH_MSG_TYPE_MAKE_SEND;
      *ports_type = MACH_MSG_TYPE_MAKE_SEND;
      if (source_addr)
	{
	  *addr = ports_get_right (source_addr);
	  ports_port_deref (source_addr); /* since get_right has one too.  */
	}
      else
	*addr = MACH_PORT_NULL;
    }

  out_flags =
    0;

  return err;
}

/* Stubs for currently unsupported rpcs.  */

error_t
S_socket_getopt (struct sock_user *user,
		 int level, int opt,
		 char **value, size_t *value_len)
{
  return EOPNOTSUPP;
}

error_t
S_socket_setopt (struct sock_user *user,
		 int level, int opt, char *value, size_t value_len)
{
  return EOPNOTSUPP;
}
