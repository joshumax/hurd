/* The SOCK_SEQPACKET pipe class

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

#include <sys/socket.h>		/* For SOCK_SEQPACKET */

#include "pipe.h"
#include "pq.h"

/* See the definition of struct pipe_class in "pipe.h" for documentation.  */

/* This type of pipe is the same as a SOCK_STREAM, but maintains record
   boundaries.  */

static error_t 
seqpack_write (struct pq *pq, void *source,
	       char *data, size_t data_len, size_t *amount)
{
  struct packet *packet = pq_queue (pq, PACKET_TYPE_DATA, source);
  if (!packet)
    return ENOBUFS;
  else
    return packet_write (packet, data, data_len, amount);
}

static error_t 
seqpack_read (struct packet *packet, int *dequeue, unsigned *flags,
	      char **data, size_t *data_len, size_t amount)
{
  error_t err;
  if (flags && *flags & MSG_PEEK)
    {
      err = packet_peek (packet, data, data_len, amount);
      *dequeue = 0;
    }
  else
    {
      err = packet_read (packet, data, data_len, amount);
      *dequeue = (packet_readable (packet) == 0);
    }
  return err;
}

struct pipe_class _seqpack_pipe_class =
{
  SOCK_SEQPACKET, 0, seqpack_read, seqpack_write
};
struct pipe_class *seqpack_pipe_class = &_seqpack_pipe_class;
