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

/* Returns the pipe that SOCK is reading from, locked and with an
   additional reference, or NULL if it has none.  SOCK mustn't be locked.  */
struct pipe *
sock_aquire_read_pipe (struct sock *sock)
{
  struct pipe *pipe;

  mutex_lock (&sock->lock);
  pipe = user->sock->read_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the sock!  */
  mutex_unlock (&sock->lock);

  return pipe;
}

/* Returns the pipe that SOCK is writing from, locked and with an
   additional reference, or NULL if it has none.  SOCK mustn't be locked.  */
struct pipe *
sock_aquire_write_pipe (struct sock *sock)
{
  struct pipe *pipe;

  mutex_lock (&sock->lock);
  pipe = user->sock->write_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the sock!  */
  mutex_unlock (&sock->lock);

  return pipe;
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

/* Return a new user port on SOCK, in PORT and PORT_TYPE.  */
error_t
sock_create_port (struct sock *sock,
		  mach_port_t *port, mach_msg_type_name_t *port_type)
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
  *port_type = MACH_MSG_TYPE_MAKE_SEND;

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
  if (sock1->pipe_ops != sock2->pipe_ops)
    return EOPNOTSUPP;		/* XXX?? */

  mutex_lock (&connect_lock);
  mutex_lock (&sock1->lock);
  mutex_lock (&sock2->lock);

  if (sock1->read_pipe || sock1->write_pipe
      || sock2->read_pipe || sock2->write_pipe)
    err = EISCONN;
  else
    {
      struct pipe *pipe1, *pipe2;
      err = pipe_create (sock1, sock2, sock1->pipe_ops, &pipe1);
      if (!err)
	{
	  err = pipe_create (sock2, sock1, sock1->pipe_ops, &pipe2);
	  if (err)
	    pipe_free (pipe1);
	}
      if (!err)
	{
	  sock1->write_pipe = sock2->read_pipe = pipe1;
	  sock2->write_pipe = sock1->read_pipe = pipe2;
	}
    }

  mutex_unlock (&sock2->lock);
  mutex_unlock (&sock1->lock);
  mutex_unlock (&connect_lock);

  return err;
}
