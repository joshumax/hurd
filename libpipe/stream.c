/* The SOCK_STREAM pipe class

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

#include <sys/socket.h>		/* For SOCK_STREAM */

#include "pipe.h"
#include "pq.h"

/* See the definition of struct pipe_class in "pipe.h" for an explanation.  */

/* This should be in some system header...  XXX  */
static inline int page_aligned (vm_offset_t num)
{
  return trunc_page (num) == num;
}

static error_t 
stream_write (struct pq *pq, void *source,
	      char *data, size_t data_len, size_t *amount)
{
  struct packet *packet = pq_tail (pq, PACKET_TYPE_DATA, source);

  if (packet_readable (packet) > 0
      && data_len > PACKET_SIZE_LARGE
      && (! page_aligned (data - packet->buf_end)
	  || ! packet_ensure_efficiently (packet, data_len)))
    /* Put a large page-aligned transfer in its own packet, if it's
       page-aligned `differently' than the end of the current packet, or if
       the current packet can't be extended in place.  */
    packet = pq_queue (pq, PACKET_TYPE_DATA, source);

  if (!packet)
    return ENOBUFS;
  else
    return packet_write (packet, data, data_len, amount);
}

static error_t 
stream_read (struct packet *packet, int *dequeue, unsigned *flags,
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

struct pipe_class _stream_pipe_class =
{
  SOCK_STREAM, 0, stream_read, stream_write
};
struct pipe_class *stream_pipe_class = &_stream_pipe_class;
