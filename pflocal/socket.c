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

#include "sock.h"
#include "connq.h"

#include "socket_S.h"

/* Connect two sockets */
error_t
S_socket_connect2 (struct sock_user *user1, struct sock_user *user2)
{
  if (!user1 || !user2)
    return EOPNOTSUPP;
  return sock_connect (user1->sock, user2->sock);
}

/* Make sure we have a queue to listen on.  */
static error_t
ensure_connq (struct sock *sock)
{
  error_t err = 0;
  mutex_lock (&sock->lock);
  if (!sock->connq)
    err = connq_create (0, &sock->connq);
  mutex_unlock (&sock->lock);
  return err;
}

error_t
S_socket_connect (struct sock_user *user, struct addr *addr)
{
  struct sock *peer;

  if (! user)
    return EOPNOTSUPP;
  if (! addr)
    return EADDRNOTAVAIL;

  err = addr_get_sock (addr, &peer);
  if (err)
    return err;

  return sock_connect (user->sock, peer);
}

/* Prepare a socket of appropriate type for future accept operations.  */
error_t
S_socket_listen (struct sock_user *user, unsigned queue_limit)
{
  error_t err;
  if (!user)
    return EOPNOTSUPP;
  err = ensure_connq (sock);
  if (!err)
    err = connq_set_length (sock->connq, queue_limit);
  return err;
}

/* Return a new connection from a socket previously listened.  */
error_t
S_socket_accept (struct sock_user *user,
		 mach_port_t *port, mach_msg_type_name_t *port_type,
		 mach_port_t *peer_addr, mach_msg_type_name_t *peer_addr_type)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  err = ensure_connq (sock);
  if (!err)
    {
      struct connq_request *req;
      struct sock *peer_sock;

      err =
	connq_listen (sock->connq, sock->flags & SOCK_NONBLOCK,
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
		  err = sock_create_port (conn_sock, port, port_type);
		  if (!err)
		    {
		      *addr = ports_get_right (peer_sock->addr);
		      *addr_type = MACH_MSG_MAKE_SEND;
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
S_socket_shutdown (struct sock_user *user, unsigned what)
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
  if (!user)
    return EOPNOTSUPP;
  *addr_port_type = MACH_MSG_MAKE_SEND;
  return sock_get_addr_port (user->sock, &addr_port);
}

/* Find out the name of the socket's peer.  */
error_t
S_socket_peername (struct sock_user *user,
		   mach_port_t *addr_port,
		   mach_msg_type_name_t *addr_port_type)
{
  if (!user)
    return EOPNOTSUPP;
  *addr_port_type = MACH_MSG_MAKE_SEND;
  return sock_get_peer_addr_port (user->sock, &addr_port);
}

/* Send data over a socket, possibly including Mach ports.  */
error_t
S_socket_send (struct sock_user *user, struct addr *dest_addr, unsigned flags,
	       char *data, size_t data_len,
	       mach_port_t *ports, size_t num_ports,
	       char *control, size_t control_len,
	       size_t *amount)
{
  struct pipe *pipe;
  struct sock *dest_sock;
  struct addr *dest_addr, *source_addr;

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
    return EINVAL;		/* ? XXX */

  err = sock_get_addr (user->sock, &source_addr);
  if (!err)
    {
      /* Grab the destination socket's read pipe directly, and stuff data
	 into it.  This is not quite the usage sock_aquire_read_pipe was
	 intended for, but it will work, as the only inappropiate errors
	 occur on a broken pipe, which shouldn't be possible with the sort of
	 sockets with which we can use socket_send...  XXXX */
      err = sock_aquire_read_pipe (dest, &pipe);
      if (!err)
	{
	  err = pipe_write (pipe, source_addr, data, data_len, amount);
	  pipe_release (pipe);
	}
      ports_port_deref (source_addr);
    }

  return err;
}

/* Receive data from a socket, possibly including Mach ports.  */
error_t
S_socket_recv (struct sock_user *user,
	       mach_port_t *addr, mach_msg_type_name_t *addr_type,
	       unsigned in_flags,
	       char **data, size_t *data_len,
	       mach_port_t **ports, mach_msg_type_name_t *ports_type,
	       size_t *num_ports,
	       char **control, size_t *control_len,
	       unsigned *out_flags, size_t amount)
{
  error_t err;
  unsigned flags;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  if (flags & MSG_OOB)
    /* BSD local sockets don't support OOB data.  */
    return EOPNOTSUPP;

  flags =
    0;

  err = sock_aquire_read_pipe (user->sock, &pipe);
  if (!err)
    {
      err =
	pipe_read (pipe, user->sock->flags & SOCK_NONBLOCK, &flags,
		   source_addr, data, data_len, amount,
		   control, control_len, ports, num_ports);
      pipe_release (pipe);
    }

  if (!err)
    /* Setup mach ports for return.  */
    {
      if (source_addr)
	{
	  *addr = ports_get_right (source_addr);
	  *addr_type = MACH_MSG_MAKE_SEND;
	  ports_port_deref (source_addr); /* since get_right gives us one.  */
	}
      if (ports && *ports_len > 0)
	*ports_type = MACH_MSG_MAKE_SEND;
    }

  out_flags =
    0;

  return err;
}

/* Stubs for currently unsupported rpcs.  */

error_t
S_socket_getopt (struct sock_user *user,
		 unsigned level, unsigned opt,
		 char **value, size_t *value_len)
{
  return EOPNOTSUPP;
}

error_t
S_socket_setopt (struct sock_user *user,
		 unsigned level, unsigned opt,
		 char *value, size_t value_len)
{
  return EOPNOTSUPP;
}
