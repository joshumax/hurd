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

error_t
sock_create (int type, struct sock *result)
{
  static unsigned next_sock_id = 0;
  struct sock *sock = malloc (sizeof (struct sock));

  if (sock == NULL)
    return ENOMEM;

  sock->refs = 0;
  sock->read_pipe = sock->write_pipe = NULL;
  sock->id = next_sock_id++;
  bzero (&sock->change_time, sizeof (sock->change_time));
  mutex_init (&sock->lock);

  switch (type)
    {
    case SOCK_STREAM:
      sock->pipe_ops = stream_pipe_ops; break;
    case SOCK_DGRAM:
      sock->pipe_ops = dgram_pipe_ops; break;
    default:
      free (sock);
      return ESOCKTNOSUPPORT;
    }

  return 0;
}
