/* Generic one-way pipes

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

#include <string.h>		/* For bzero() */
#include <assert.h>

#include <mach/time_value.h>
#include <mach/mach_host.h>

#include "pipe.h"

static inline void
timestamp (time_value_t *stamp)
{
  host_get_time (mach_host_self (), stamp);
}

/* ---------------------------------------------------------------- */

/* Creates a new pipe of class CLASS and returns it in RESULT.  */
error_t
pipe_create (struct pipe_class *class, struct pipe **pipe)
{
  struct pipe *new = malloc (sizeof (struct pipe));

  if (new == NULL)
    return ENOMEM;

  new->readers = 0;
  new->writers = 0;
  new->flags = 0;
  new->class = class;

  bzero (&new->read_time, sizeof (new->read_time));
  bzero (&new->write_time, sizeof (new->write_time));

  condition_init (&new->pending_reads);
  condition_init (&new->pending_selects);
  mutex_init (&new->lock);

  pq_create (&new->queue);  

  *pipe = new;
  return 0;
}

/* Free PIPE and any resources it holds.  */
void 
pipe_free (struct pipe *pipe)
{
  pq_free (pipe->queue);
  free (pipe);
}

/* Wake up all threads waiting on PIPE, which should be locked.  */
inline void
pipe_kick (struct pipe *pipe)
{
  /* Now wake them all up for the bad news... */
  condition_broadcast (&pipe->pending_reads);
  mutex_unlock (&pipe->lock);
  condition_broadcast (&pipe->pending_selects);
  mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
}

/* Take any actions necessary when PIPE acquires its first writer.  */
void _pipe_first_writer (struct pipe *pipe)
{
  if (! (pipe->class->flags & PIPE_CLASS_CONNECTIONLESS))
    pipe->flags &= ~PIPE_BROKEN;
}

/* Take any actions necessary when PIPE's last reader has gone away.  PIPE
   should be locked.  */
void _pipe_no_readers (struct pipe *pipe)
{
  if (pipe->writers == 0)
    pipe_free (pipe);
  else
    mutex_unlock (&pipe->lock);
}

/* Take any actions necessary when PIPE's last writer has gone away.  PIPE
   should be locked.  */
void _pipe_no_writers (struct pipe *pipe)
{
  if (pipe->readers == 0)
    pipe_free (pipe);
  else
    {
      if (! (pipe->class->flags & PIPE_CLASS_CONNECTIONLESS))
	{
	  pipe->flags |= PIPE_BROKEN;
	  if (pipe->readers)
	    /* Wake up readers who might want to know about our new state.  */
	    pipe_kick (pipe);
	}
      mutex_unlock (&pipe->lock);
    }
}

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If present, the information in
   CONTROL & PORTS is written in a preceding control packet.  If an error is
   returned, nothing is done.  */
error_t
pipe_send (struct pipe *pipe, void *source,
	   char *data, size_t data_len,
	   char *control, size_t control_len,
	   mach_port_t *ports, size_t num_ports,
	   size_t *amount)
{
  error_t err = 0;

  if (pipe->flags & PIPE_BROKEN)
    return EPIPE;

  if (control_len > 0 || num_ports > 0)
    /* Write a control packet.  */
    {
      /* Note that we don't record the source address in control packets, as
	 it's recorded in the following data packet anyway, and this prevents
	 it from being dealloc'd twice; this depends on the fact that we
	 always write a data packet.  */
      struct packet *control_packet =
	pq_queue (pipe->queue, PACKET_TYPE_CONTROL, NULL);

      if (control_packet == NULL)
	err = ENOBUFS;
      else
	{
	  err = packet_write (control_packet, control, control_len, NULL);
	  if (!err)
	    err = packet_set_ports (control_packet, ports, num_ports);
	  if (err)
	    /* Trash CONTROL_PACKET somehow XXX */;
	}
    }

  if (!err)
    err = (*pipe->class->write)(pipe->queue, source, data, data_len, amount);

  if (!err)
    {
      timestamp (&pipe->write_time);
      
      /* And wakeup anyone that might be interested in it.  */
      condition_broadcast (&pipe->pending_reads);
      mutex_unlock (&pipe->lock);

      mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
      /* Only wakeup selects if there's still data available.  */
      if (pipe_is_readable (pipe, 0))
	{
	  condition_broadcast (&pipe->pending_selects);
	  /* We leave PIPE locked here, assuming the caller will soon unlock
	     it and allow others access.  */
	}
    }

  return err;
}

/* Reads up to AMOUNT bytes from PIPE, which should be locked, into DATA, and
   returns the amount read in DATA_LEN.  If NOBLOCK is true, EWOULDBLOCK is
   returned instead of block when no data is immediately available.  If an
   error is returned, nothing is done.  If source isn't NULL, the address of
   the socket from which the data was sent is returned in it; this may be
   NULL if it wasn't specified by the sender (which is usually the case with
   connection-oriented protcols).

   If there is control data waiting (before any data), then the behavior
   depends on whether this is an `ordinary read' (when CONTROL and PORTS are
   both NULL), in which case any control data is skipped, or a `msg read', in
   which case the contents of the first control packet is returned (in
   CONTROL and PORTS), as well as the first data packet following that (if
   the control packet is followed by another control packet or no packet in
   this case, a zero length data buffer is returned; the user should be
   careful to distinguish this case from EOF (when no control or ports data
   is returned either).  */
error_t
pipe_recv (struct pipe *pipe, int noblock, unsigned *flags, void **source,
	   char **data, size_t *data_len, size_t amount,
	   char **control, size_t *control_len,
	   mach_port_t **ports, size_t *num_ports)
{
  error_t err;
  struct packet *packet;
  struct pq *pq = pipe->queue;
  /* True if the user isn't asking for any `control' data.  */
  int data_only = (control == NULL && ports == NULL);

  err = pipe_wait (pipe, noblock, data_only);
  if (err)
    return err;

  packet = pq_head (pq, PACKET_TYPE_ANY, 0);

  if (data_only)
    /* The user doesn't want to know about control info, so skip any...  */
    while (packet && packet->type == PACKET_TYPE_CONTROL)
      packet = pq_next (pq, PACKET_TYPE_ANY, 0);
  else if (packet && packet->type == PACKET_TYPE_CONTROL)
    /* Read this control packet first, before looking for a data packet. */
    {
      if (control != NULL)
	packet_read (packet, control, control_len, packet_readable (packet));
      if (ports != NULL)
	/* Copy out the port rights being sent.  */
	packet_read_ports (packet, ports, num_ports);

      packet = pq_next (pq, PACKET_TYPE_DATA, NULL);
      assert (packet);		/* pipe_write always writes a data packet.  */
    }
  else
    /* No control data... */
    {
      if (control_len)
	*control_len = 0;
      if (num_ports)
	*num_ports = 0;
    }

  if (!err)
    if (packet)
      /* Read some data (PACKET must be a data packet at this point).  */
      {
	int dq = 1;	/* True if we should dequeue this packet.  */

 	if (source)
	  packet_read_source (packet, source);

	err = (*pipe->class->read)(packet, &dq, flags, data, data_len, amount);
	if (dq)
	  pq_dequeue (pq);
      }
    else
      /* Return EOF.  */
      *data_len = 0;

  if (!err && packet)
    timestamp (&pipe->read_time);

  return err;
}
