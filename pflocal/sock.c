/* Sock functions

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

#include "pflocal.h"

/* ---------------------------------------------------------------- */

/* Returns the pipe that SOCK is reading from in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  A null
   value may also be returned in PIPE with a 0 error, meaning that EOF should
   be returned.  SOCK mustn't be locked.  */
error_t
sock_aquire_read_pipe (struct sock *sock, struct pipe **pipe)
{
  error_t err;

  mutex_lock (&sock->lock);
  *pipe = user->sock->read_pipe;
  if (*pipe != NULL)
    pipe_aquire (*pipe);	/* Do this before unlocking the sock!  */
  else if (! (sock->flags & SOCK_SHUTDOWN_READ))
    /* We only return ENOTCONN if a shutdown hasn't been performed.  */
    err = ENOTCONN;
  mutex_unlock (&sock->lock);

  return err;
}

/* Returns the pipe that SOCK is writing to in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t
sock_aquire_write_pipe (struct sock *sock, struct pipe **pipe)
{
  error_t err = 0;

  mutex_lock (&sock->lock);
  *pipe = user->sock->write_pipe;
  if (*pipe != NULL)
    pipe_aquire (*pipe);	/* Do this before unlocking the sock!  */
  else if (sock->flags & SOCK_SHUTDOWN_WRITE)
    err = EPIPE;
  else
    err = ENOTCONN;
  mutex_unlock (&sock->lock);

  return err;
}

/* ---------------------------------------------------------------- */

/* Return a new socket with the given pipe ops in SOCK.  */
error_t
sock_create (struct pipe_ops *pipe_ops, struct sock **sock)
{
  static unsigned next_sock_id = 0;
  struct sock *new = malloc (sizeof (struct sock));

  if (new == NULL)
    return ENOMEM;

  new->refs = 0;
  new->read_pipe = new->write_pipe = NULL;
  new->flags = 0;
  new->id = next_sock_id++;
  new->pipe_ops = pipe_ops;
  new->listenq = NULL;
  new->addr = NULL;
  bzero (&new->change_time, sizeof (new->change_time));
  mutex_init (&new->lock);

  *sock = new;
  return 0;
}

void
sock_free (struct sock *sock)
{
  
}

/* Return a new user port on SOCK in PORT.  */
error_t
sock_create_port (struct sock *sock, mach_port_t *port)
{
  struct sock_user *user =
    port_allocate_port (sock_user_bucket,
			sizeof (struct sock_user), sock_user_class);

  if (!user)
    return ENOMEM;

  mutex_lock (&sock->lock);
  sock->refs++;
  mutex_unlock (&sock->lock);

  user->sock = sock;

  *port = ports_get_right (user);

  return 0;
}

/* ---------------------------------------------------------------- */

/* We hold this lock when we want to lock both sockets for a attach
   operation, to avoid another attach with the sockets reversed happening.
   Attach should be the only place trying to lock two sockets at once, so
   this should be safe...  */
static struct mutex connect_lock;

/* Connect together the previously unconnected sockets SOCK1 and SOCK2.  */
error_t
sock_connect (struct sock *sock1, struct sock *sock2)
{
  error_t err = 0;
  struct pipe_class *pipe_class = sock1->pipe_class;

  if (sock2->pipe_class != pipe_class)
    return EOPNOTSUPP;		/* XXX?? */

  mutex_lock (&connect_lock);
  mutex_lock (&sock1->lock);
  mutex_lock (&sock2->lock);

  if ((sock1->flags & SOCK_CONNECTED) || (sock2->flags & SOCK_CONNECTED))
    err = EISCONN;
  else
    {
      struct pipe *pipe1, *pipe2;

      /* Make one direction.... */
      if ((sock1->flags & SOCK_SHUTDOWN_WRITE)
	  || (sock2->flags & SOCK_SHUTDOWN_READ))
	pipe1 = NULL;
      else
	err = pipe_create (pipe_class, &pipe1);

      /* Then the other...  */
      if (!err)
	{
	  if ((sock2->flags & SOCK_SHUTDOWN_WRITE)
	      || (sock1->flags & SOCK_SHUTDOWN_READ))
	    pipe2 = NULL;
	  else
	    err = pipe_create (pipe_class, &pipe2);

	  if (err)
	    pipe_free (pipe1);
	  else
	    {
	      sock1->write_pipe = sock2->read_pipe = pipe1;
	      sock2->write_pipe = sock1->read_pipe = pipe2;
	    }
	}
    }

  mutex_unlock (&sock2->lock);
  mutex_unlock (&sock1->lock);
  mutex_unlock (&connect_lock);

  return err;
}

/* ---------------------------------------------------------------- */

void
sock_shutdown (struct sock *sock, unsigned flags)
{
  mutex_lock (&sock->lock);

  sock->flags |= flags;

  if (which & SOCK_SHUTDOWN_READ)
    /* Shutdown the read half.  */
    {
      struct pipe *pipe = sock->read_pipe;
      if (pipe != NULL)
	{
	  sock->read_pipe = NULL;
	  mutex_lock (&pipe->lock);
	  pipe->flags |= PIPE_BROKEN;
	  pipe_release (pipe); /* Unlock PIPE and get rid of SOCK's ref.  */
	}
    }

  if (which & SOCK_SHUTDOWN_WRITE)
    /* Shutdown the write half.  */
    {
      struct pipe *pipe = sock->write_pipe;
      if (pipe != NULL)
	{
	  sock->write_pipe = NULL;

	  mutex_lock (&pipe->lock);

	  /* As there may be multiple writers on a connectionless socket, we
	     never allow EOF to be signaled on the reader.  */
	  if (! (pipe->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS))
	    pipe->flags |= PIPE_DRY;

	  /* Unlock SOCK here, as we may subsequently wake up other threads. */
	  mutex_unlock (&sock->lock);

	  if (pipe->refs > 1)
	    /* Other references to PIPE besides ours?  Wake 'em up.  */
	    pipe_kick (pipe);

	  pipe_release (pipe);
	}
      else
	mutex_unlock (&sock->lock);
    }
  else
    mutex_unlock (&sock->lock);
}
