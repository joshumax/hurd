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
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t
sock_aquire_read_pipe (struct sock *sock, struct pipe **pipe)
{
  error_t err;

  mutex_lock (&sock->lock);

  *pipe = user->sock->read_pipe;
  assert (*pipe);		/* A socket always has a read pipe.  */

  if (((*pipe)->flags & PIPE_BROKEN) && ! (sock->flags & SOCK_SHUTDOWN_READ))
    err = ENOTCONN;
  else
    pipe_aquire (*pipe);

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
    /* Writing on a socket with the write-half shutdown always acts as if the
       pipe we're broken, even if the socket isn't connected yet.  */
    err = EPIPE;
  else if (sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
    /* Connectionless protocols give a different error when unconnected.  */
    err = EDESTADDRREQ;
  else
    err = ENOTCONN;
  mutex_unlock (&sock->lock);

  return err;
}

/* ---------------------------------------------------------------- */

/* Return a new socket with the given pipe class in SOCK.  */
error_t
sock_create (struct pipe_class *pipe_class, struct sock **sock)
{
  error_t err;
  static unsigned next_sock_id = 0;
  struct sock *new = malloc (sizeof (struct sock));

  if (new == NULL)
    return ENOMEM;

  /* A socket always has a read pipe, so create it here.  */
  err = pipe_create (pipe_class, &new->read_pipe);
  if (err)
    {
      free (new);
      return err;
    }
  if (! (pipe_class->flags & PIPE_CLASS_CONNECTIONLESS))
    /* No data source yet.  */
    new->read_pipe->flags |= PIPE_BROKEN;

  new->refs = 0;
  new->flags = 0;
  new->write_pipe = NULL;
  new->id = next_sock_id++;
  new->listenq = NULL;
  new->addr = NULL;
  bzero (&new->change_time, sizeof (new->change_time));
  mutex_init (&new->lock);

  *sock = new;
  return 0;
}

/* Free SOCK, assuming there are no more handle on it.  */
void
sock_free (struct sock *sock)
{
  /* sock_shutdown will get rid of the write pipe.  */
  sock_shutdown (sock, SOCK_SHUTDOWN_READ | SOCK_SHUTDOWN_WRITE);

  /* But we must do the read pipe ourselves.  */
  pipe_release (sock->read_pipe);

  free (sock);
}

/* Remove a reference from SOCK, possibly freeing it.  */
void
sock_unref (struct sock *sock)
{
  mutex_lock (&sock->lock);
  if (--sock->refs == 0)
    sock_free (sock);
  else
    mutex_unlock (&sock->lock);
}

/* ---------------------------------------------------------------- */

static struct port_class *sock_user_port_class = NULL;

/* Get rid of a user reference to a socket.  */
static void
clean_sock_user (void *vuser)
{
  struct sock_user *user = vuser;
  sock_unref (user->sock);
}

/* Return a new user port on SOCK in PORT.  */
error_t
sock_create_port (struct sock *sock, mach_port_t *port)
{
  struct sock_user *user;

  if (sock_user_port_class == NULL)
    sock_user_port_class = ports_create_class (NULL, clean_sock_user);

  user =      
    port_allocate_port (pflocal_port_bucket,
			sizeof (struct sock_user), sock_user_port_class);

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
  /* In the case of a connectionless protocol, an already-connected socket may
     be reconnected elsewhere, so we save the old write pipe for later
     disposal.  */
  struct pipe *old_sock1_write_pipe = NULL;
  struct pipe_class *pipe_class = sock1->read_pipe->pipe_class;
  /* True if this protocol is a connectionless one.  */
  int connless = (pipe_class->flags & PIPE_CLASS_CONNECTIONLESS);

  int connected (struct sock *s)
    {
      /* A socket is considered connected if it has a write pipe or a
	 non-broken read-pipe.  */
      return s->write_pipe != NULL || ! (s->read_pipe->flags & PIPE_BROKEN);
    }
  void connect (struct sock *wr, struct sock *rd)
    {
      if ((wr->flags & SOCK_SHUTDOWN_WRITE)
	  || (rd->flags & SOCK_SHUTDOWN_READ))
	{
	  struct pipe *pipe = rd->read_pipe;
	  pipe_aquire (pipe);
	  pipe->flags &= ~PIPE_BROKEN; /* Not yet...  */
	  wr->write_pipe = pipe;
	  mutex_unlock (&pipe->lock);
	}
    }

  if (sock2->read_pipe->pipe_class != pipe_class)
    /* Incompatible socket types.  */
    return EOPNOTSUPP;		/* XXX?? */

  mutex_lock (&connect_lock);
  mutex_lock (&sock1->lock);
  mutex_lock (&sock2->lock);

  if (!connless && (connected (sock1) || connected (sock2)))
    err = EISCONN;
  else
    {
      old_sock1_write_pipe = sock1->write_pipe;

      /* We always try and make the forward connection.  */
      connect (sock1, sock2);

      /* We only make the backward connection for connection-oriented
	 protocols.  */
      if (! (pipe_class->flags & PIPE_CLASS_CONNECTIONLESS))
	connect (sock2, sock1);
    }

  mutex_unlock (&sock2->lock);
  mutex_unlock (&sock1->lock);
  mutex_unlock (&connect_lock);

  if (old_sock1_write_pipe)
    /* Discard SOCK1's previous write pipe.  */
    pipe_discard (old_sock1_write_pipe);

  return err;
}

/* ---------------------------------------------------------------- */

/* Bind SOCK to ADDR.  */
error_t
sock_bind (struct sock *sock, struct addr *addr)
{
  error_t err;

  mutex_lock (&sock->lock);

  if (sock->addr)
    if (addr)
      err = EINVAL;		/* Already bound.  */
    else
      err = addr_set_sock (sock->addr, NULL);
  else
    if (addr)
      err = addr_set_sock (addr, sock);

  if (!err)
    sock->addr = addr;

  mutex_unlock (&sock->lock);

  return err;
}

/* Returns SOCK's address in ADDR.  If SOCK doesn't currently have an
   address, one is fabricated first.  */
error_t
sock_get_addr (struct sock *sock, struct addr **addr)
{
  error_t err;
  mutex_lock (&sock->lock);
  if (sock->addr == NULL)
    err = addr_create (&sock->addr);
  *addr = sock->addr;
  mutex_unlock (&sock->lock);
  return err;
}

error_t
sock_get_peer_addr (struct sock *sock, struct addr **addr)
{
}

/* ---------------------------------------------------------------- */

void
sock_shutdown (struct sock *sock, unsigned flags)
{
  mutex_lock (&sock->lock);

  sock->flags |= flags;

  if (which & SOCK_SHUTDOWN_READ)
    /* Shutdown the read half.  We keep the pipe around though.  */
    {
      struct pipe *pipe = sock->read_pipe;
      mutex_lock (&pipe->lock);
      /* This will prevent any further writes to PIPE.  */
      pipe->flags |= PIPE_BROKEN;
      /* Make sure subsequent reads return EOF.  */
      pipe_drain (pipe);
      mutex_unlock (&pipe->lock);
    }

  if (which & SOCK_SHUTDOWN_WRITE)
    /* Shutdown the write half.  */
    {
      struct pipe *pipe = sock->write_pipe;
      if (pipe != NULL)
	{
	  sock->write_pipe = NULL;
	  /* Unlock SOCK here, as we may subsequently wake up other threads. */
	  mutex_unlock (&sock->lock);
	  pipe_discard (pipe);
	}
      else
	mutex_unlock (&sock->lock);
    }
  else
    mutex_unlock (&sock->lock);
}

