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

#include <socket.h>

#include "pflocal.h"

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

/* Prepare a socket of appropriate type for future accept operations.  */
error_t
S_socket_listen (struct sock_user *user, int queue_limit)
{
  error_t err;
  if (!user)
    return EOPNOTSUPP;
  err = ensure_connq (sock);
  if (!err)
    err = connq_set_length (sock->connq, queue_limit);
  return err;
}

error_t
S_socket_connect (struct sock_user *user, struct addr *addr)
{
  if (! user)
    return EOPNOTSUPP;
  if (!addr)
    return EADDRNOTAVAIL;

  
}

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

/* Stubs for currently unsupported rpcs.  */

error_t
S_socket_getopt (struct sock_user *user,
		 int level, int opt,
		 char **value, unsigned *value_len)
{
  return EOPNOTSUPP;
}

error_t
S_socket_setopt (struct sock_user *user,
		 int level, int opt,
		 char *value, unsigned value_len)
{
  return EOPNOTSUPP;
}

error_t
S_socket_send (struct sock_user *user,
	       mach_port_t addr, int flags,
	       char *data, unsigned data_len,
	       mach_port_t *ports, unsigned num_ports,
	       char *control, unsigned control_len,
	       int *amount)
{
  return EOPNOTSUPP;
}

error_t
S_socket_recv (struct sock_user *user,
	       mach_port_t *addr, int flags,
	       char **data, unsigned *data_len,
	       mach_port_t **ports, mach_msg_type_name_t *ports_type,
	       unsigned *num_ports,
	       char **control, unsigned *control_len,
	       int *out_flags, int amount)
{
  return EOPNOTSUPP;
}
