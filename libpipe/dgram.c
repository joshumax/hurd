/* The SOCK_DGRAM pipe class

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

#include <sys/socket.h>		/* For SOCK_DGRAM */

#include "pipe.h"
#include "pq.h"

/* See the definition of struct pipe_class in "pipe.h" for documentation.  */

static error_t 
dgram_write (struct pq *pq, void *source,
	     char *data, size_t data_len, size_t *amount)
{
  struct packet *packet = pq_queue (pq, PACKET_TYPE_DATA, source);
  if (!packet)
    return ENOBUFS;
  else
    return packet_write (packet, data, data_len, amount);
}

static error_t 
dgram_read (struct packet *packet, int *dequeue, unsigned *flags,
	    char **data, size_t *data_len, size_t amount)
{
  if (flags && *flags & MSG_PEEK)
    {
      *dequeue = 0;
      return packet_peek (packet, data, data_len, amount);
    }
  else
    {
      *dequeue = 1;
      return packet_read (packet, data, data_len, amount);
    }
}

struct pipe_class _dgram_pipe_class =
{
  SOCK_DGRAM, PIPE_CLASS_CONNECTIONLESS, dgram_read, dgram_write
};
struct pipe_class *dgram_pipe_class = &_dgram_pipe_class;
