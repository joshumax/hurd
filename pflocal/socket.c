/* Socket-specific operations

   Copyright (C) 1995, 2008, 2010, 2012 Free Software Foundation, Inc.

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
  if (!err && user1->sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
    err = sock_connect (user2->sock, user1->sock);

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
  pthread_mutex_lock (&sock->lock);
  if (!sock->listen_queue)
    err = connq_create (&sock->listen_queue);
  pthread_mutex_unlock (&sock->lock);
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

  if (! addr)
    return ECONNREFUSED;

  /* Deallocate ADDR's send right, which we get as a side effect of the rpc. */
  mach_port_deallocate (mach_task_self (),
			((struct port_info *)addr)->port_right);

  if (! user)
    return EOPNOTSUPP;

  err = addr_get_sock (addr, &peer);
  if (err == EADDRNOTAVAIL)
    /* The server went away.  */
    err = ECONNREFUSED;
  else if (!err)
    {
      struct sock *sock = user->sock;
      struct connq *cq = peer->listen_queue;

      if (sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
	/* For connectionless protocols, connect() just sets where writes
	   will go, so the destination need not be doing an accept.  */
	err = sock_connect (sock, peer);
      else if (cq)
	/* For connection-oriented protocols, only connect with sockets that
           are actually listening.  */
	{
	  pthread_mutex_lock (&sock->lock);
	  if (sock->connect_queue)
	    /* SOCK is already doing a connect.  */
	    err = EALREADY;
	  else if (sock->flags & PFLOCAL_SOCK_CONNECTED)
	    /* PFLOCAL_SOCK_CONNECTED is only set for connection-oriented sockets,
	       which can only ever connect once.  [If we didn't do this test
	       here, it would eventually fail when the listening socket
	       tried to accept our connection request.]  */
	    err = EISCONN;
	  else
	    {
	      /* Assert that we're trying to connect, so anyone else trying
	         to do so will fail with EALREADY.  */
	      sock->connect_queue = cq;
	      /* Unlock SOCK while waiting.  */
	      pthread_mutex_unlock (&sock->lock);

	      err = connq_connect (peer->listen_queue,
				   sock->flags & PFLOCAL_SOCK_NONBLOCK);
	      if (!err)
		{
		  struct sock *server;

		  err = sock_clone (peer, &server);
		  if (!err)
		    {
		      err = sock_connect (sock, server);
		      if (!err)
			connq_connect_complete (peer->listen_queue, server);
		      else
			sock_free (server);
		    }

		  if (err)
		    connq_connect_cancel (peer->listen_queue);
		}

              pthread_mutex_lock (&sock->lock);
	      /* We must set CONNECT_QUEUE to NULL, as no one else can
		 set it until we've done so.  */
	      sock->connect_queue = NULL;
	    }

	  pthread_mutex_unlock (&sock->lock);
	}
      else
	err = ECONNREFUSED;

      sock_deref (peer);
    }

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
      struct timespec noblock = {0, 0};
      struct sock *peer_sock;

      err = connq_listen (sock->listen_queue,
			  (sock->flags & PFLOCAL_SOCK_NONBLOCK) ? &noblock : NULL,
			  &peer_sock);
      if (!err)
	{
	  struct addr *peer_addr;
	  *port_type = MACH_MSG_TYPE_MAKE_SEND;
	  err = sock_create_port (peer_sock, port);
	  if (!err)
	    err = sock_get_addr (peer_sock, &peer_addr);
	  if (!err)
	    {
	      *peer_addr_port = ports_get_right (peer_addr);
	      *peer_addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
	      ports_port_deref (peer_addr);
	    }
	  else
	    {
	      /* TEAR DOWN THE CONNECTION XXX */
	    }
	}
    }

  return err;
}

/* Bind a socket to an address.  */
error_t
S_socket_bind (struct sock_user *user, struct addr *addr)
{
  if (! addr)
    return EADDRNOTAVAIL;

  /* Deallocate ADDR's send right, which we get as a side effect of the rpc. */
  mach_port_deallocate (mach_task_self (),
			((struct port_info *)addr)->port_right);

  if (! user)
    return EOPNOTSUPP;

  return sock_bind (user->sock, addr);
}

/* Shutdown a socket for reading or writing.  */
error_t
S_socket_shutdown (struct sock_user *user, int what)
{
  if (! user)
    return EOPNOTSUPP;
  sock_shutdown (user->sock,
		   (what != 1 ? PFLOCAL_SOCK_SHUTDOWN_READ : 0)
		 | (what != 0 ? PFLOCAL_SOCK_SHUTDOWN_WRITE : 0));
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
  ports_port_deref (addr);

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
	       data_t data, size_t data_len,
	       mach_port_t *ports, size_t num_ports,
	       data_t control, size_t control_len,
	       size_t *amount)
{
  error_t err = 0;
  int noblock;
  struct pipe *pipe;
  struct sock *sock, *dest_sock;
  struct addr *source_addr;

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;

  if (flags & MSG_OOB)
    /* BSD local sockets don't support OOB data.  */
    return EOPNOTSUPP;

  if (dest_addr)
    {
      err = addr_get_sock (dest_addr, &dest_sock);
      if (err == EADDRNOTAVAIL)
	/* The server went away.  */
	err = ECONNREFUSED;
      if (err)
	return err;
      if (sock->pipe_class != dest_sock->pipe_class)
	/* Sending to a different type of socket!  */
	err = EINVAL;		/* ? XXX */
    }
  else
    dest_sock = 0;

  /* We could provide a source address for all writes, but we
     only do so for connectionless sockets because that's the
     only place it's required, and it's more efficient not to.  */
  if (!err && sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
    err = sock_get_addr (sock, &source_addr);
  else
    source_addr = NULL;

  if (!err)
    {
      if (dest_sock)
	/* Grab the destination socket's read pipe directly, and stuff data
	   into it.  This is not quite the usage sock_acquire_read_pipe was
	   intended for, but it will work, as the only inappropriate errors
	   occur on a broken pipe, which shouldn't be possible with the sort of
	   sockets with which we can use socket_send...  XXXX */
	err = sock_acquire_read_pipe (dest_sock, &pipe);
      else
	/* No address, must be a connected socket...  */
	err = sock_acquire_write_pipe (sock, &pipe);
	
      if (!err)
	{
	  noblock = (user->sock->flags & PFLOCAL_SOCK_NONBLOCK)
		    || (flags & MSG_DONTWAIT);
	  err = pipe_send (pipe, noblock, source_addr, data, data_len,
			   control, control_len, ports, num_ports,
			   amount);
	  if (dest_sock)
	    pipe_release_reader (pipe);
	  else
	    pipe_release_writer (pipe);
	}

      if (err)
	/* The send failed, so free any resources it would have consumed
	   (mig gets rid of memory, but we have to do everything else). */
	{
	  if (source_addr)
	    ports_port_deref (source_addr);
	  while (num_ports-- > 0)
	    mach_port_deallocate (mach_task_self (), *ports++);
	}
    }

  if (dest_sock)
    sock_deref (dest_sock);

  return err;
}

/* Receive data from a socket, possibly including Mach ports.  */
error_t
S_socket_recv (struct sock_user *user,
	       mach_port_t *addr, mach_msg_type_name_t *addr_type,
	       int in_flags,
	       data_t *data, size_t *data_len,
	       mach_port_t **ports, mach_msg_type_name_t *ports_type,
	       size_t *num_ports,
	       data_t *control, size_t *control_len,
	       int *out_flags, size_t amount)
{
  error_t err;
  unsigned flags;
  int noblock;
  struct pipe *pipe;
  void *source_addr = NULL;

  if (!user)
    return EOPNOTSUPP;

  if (in_flags & MSG_OOB)
    /* BSD local sockets don't support OOB data.  */
    return EINVAL;		/* XXX */

  /* Fill in the pipe FLAGS from any corresponding ones in IN_FLAGS.  */
  flags = in_flags & MSG_PEEK;

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
      noblock = (user->sock->flags & PFLOCAL_SOCK_NONBLOCK)
		|| (in_flags & MSG_DONTWAIT);
      err =
	pipe_recv (pipe, noblock, &flags, &source_addr, data, data_len,
		   amount, control, control_len, ports, num_ports);
      pipe_release_reader (pipe);
    }

  if (!err)
    /* Setup mach ports for return.  */
    {
      *addr_type = MACH_MSG_TYPE_MAKE_SEND;
      *ports_type = MACH_MSG_TYPE_MOVE_SEND;
      if (source_addr)
	{
	  *addr = ports_get_right (source_addr);
	  ports_port_deref (source_addr); /* since get_right has one too.  */
	}
      else
	*addr = MACH_PORT_NULL;
    }

  *out_flags = 0;

  return err;
}

error_t
S_socket_getopt (struct sock_user *user,
		 int level, int opt,
		 data_t *value, size_t *value_len)
{
  int ret = 0;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->sock->lock);
  switch (level)
    {
    case SOL_SOCKET:
      switch (opt)
	{
	case SO_TYPE:
	  if (*value_len < sizeof (int))
	    {
	      ret = EINVAL;
	      break;
	    }
	  *(int *)*value = user->sock->pipe_class->sock_type;
	  *value_len = sizeof (int);
	  break;
	case SO_ERROR:
	  /* We do not have asynchronous operations (such as connect), so no
	     error to report.  */
	  if (*value_len < sizeof (short))
	  {
	    *(char*)*value = 0;
	    *value_len = sizeof(char);
	  }
	  else if (*value_len < sizeof (int))
	  {
	    *(short*)*value = 0;
	    *value_len = sizeof(short);
	  }
	  else
	  {
	    *(int*)*value = 0;
	    *value_len = sizeof(int);
	  }
	  break;
	default:
	  ret = ENOPROTOOPT;
	  break;
	}
      break;
    default:
      ret = ENOPROTOOPT;
      break;
    }
  pthread_mutex_unlock (&user->sock->lock);

  return ret;
}

error_t
S_socket_setopt (struct sock_user *user,
		 int level, int opt, data_t value, size_t value_len)
{
  int ret = 0;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->sock->lock);
  switch (level)
    {
    default:
      ret = ENOPROTOOPT;
      break;
    }
  pthread_mutex_unlock (&user->sock->lock);

  return ret;
}
