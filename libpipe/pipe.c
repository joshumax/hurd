/* Generic one-way pipes

   Copyright (C) 1995, 1998 Free Software Foundation, Inc.

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

#include <string.h>		/* For memset() */
#include <assert-backtrace.h>
#include <stdlib.h>

#include <mach/time_value.h>
#include <mach/mach_host.h>

#include <hurd/hurd_types.h>

#include "pipe.h"

static inline void
timestamp (time_value_t *stamp)
{
  host_get_time (mach_host_self (), stamp);
}

/* Hold this lock before attempting to lock multiple pipes. */
pthread_mutex_t pipe_multiple_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------- */

#define pipe_is_connless(p) ((p)->class->flags & PIPE_CLASS_CONNECTIONLESS)

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
  new->write_limit = 16*1024;
  new->write_atomic = 16*1024;

  memset (&new->read_time, 0, sizeof(new->read_time));
  memset (&new->write_time, 0, sizeof(new->write_time));

  pthread_cond_init (&new->pending_reads, NULL);
  pthread_cond_init (&new->pending_read_selects, NULL);
  pthread_cond_init (&new->pending_writes, NULL);
  pthread_cond_init (&new->pending_write_selects, NULL);
  new->pending_selects = NULL;
  pthread_mutex_init (&new->lock, NULL);

  pq_create (&new->queue);

  if (! pipe_is_connless (new))
    new->flags |= PIPE_BROKEN;

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

static void
pipe_add_select_cond (struct pipe *pipe, struct pipe_select_cond *cond)
{
  struct pipe_select_cond *first, *last;

  first = pipe->pending_selects;

  if (first == NULL)
    {
      cond->next = cond;
      cond->prev = cond;
      pipe->pending_selects = cond;
      return;
    }

  last = first->prev;
  cond->next = first;
  cond->prev = last;
  first->prev = cond;
  last->next = cond;
}

static void
pipe_remove_select_cond (struct pipe *pipe, struct pipe_select_cond *cond)
{
  cond->prev->next = cond->next;
  cond->next->prev = cond->prev;

  if (pipe->pending_selects == cond)
    {
      if (cond->next == cond)
	pipe->pending_selects = NULL;
      else
        pipe->pending_selects = cond->next;
    }
}

static void
pipe_select_cond_broadcast (struct pipe *pipe)
{
  struct pipe_select_cond *cond, *last;

  cond = pipe->pending_selects;

  if (cond == NULL)
    return;

  last = cond->prev;

  do
    {
      pthread_cond_broadcast (&cond->cond);
      cond = cond->next;
    }
  while (cond != last);
}

/* Take any actions necessary when PIPE acquires its first writer.  */
void _pipe_first_writer (struct pipe *pipe)
{
  if (pipe->readers > 0)
    pipe->flags &= ~PIPE_BROKEN;
}

/* Take any actions necessary when PIPE acquires its first reader.  */
void _pipe_first_reader (struct pipe *pipe)
{
  if (pipe->writers > 0)
    pipe->flags &= ~PIPE_BROKEN;
}

/* Take any actions necessary when PIPE's last reader has gone away.  PIPE
   should be locked.  */
void _pipe_no_readers (struct pipe *pipe)
{
  if (pipe->writers == 0)
    pipe_free (pipe);
  else
    {
      /* When there is no reader, we have to break pipe even for
         connection-less pipes.  */
      pipe->flags |= PIPE_BROKEN;
      if (pipe->writers)
	/* Wake up writers for the bad news... */
	{
	  pthread_cond_broadcast (&pipe->pending_writes);
	  pthread_cond_broadcast (&pipe->pending_write_selects);
	  pipe_select_cond_broadcast (pipe);
	}
      pthread_mutex_unlock (&pipe->lock);
    }
}

/* Take any actions necessary when PIPE's last writer has gone away.  PIPE
   should be locked.  */
void _pipe_no_writers (struct pipe *pipe)
{
  if (pipe->readers == 0)
    pipe_free (pipe);
  else
    {
      if (! pipe_is_connless (pipe))
	{
	  pipe->flags |= PIPE_BROKEN;
	  if (pipe->readers)
	    /* Wake up readers for the bad news... */
	    {
	      pthread_cond_broadcast (&pipe->pending_reads);
	      pthread_cond_broadcast (&pipe->pending_read_selects);
	      pipe_select_cond_broadcast (pipe);
	    }
	}
      pthread_mutex_unlock (&pipe->lock);
    }
}

/* Return when either RPIPE is available for reading (if SELECT_READ is set
   in *SELECT_TYPE), or WPIPE is available for writing (if select_write is
   set in *SELECT_TYPE).  *SELECT_TYPE is modified to reflect which (or both)
   is now available.  DATA_ONLY should be true if only data packets should be
   waited for on RPIPE.  Neither RPIPE or WPIPE should be locked when calling
   this function (unlike most pipe functions).  */
error_t
pipe_pair_select (struct pipe *rpipe, struct pipe *wpipe,
		  struct timespec *tsp, int *select_type, int data_only)
{
  error_t err = 0;

  *select_type &= SELECT_READ | SELECT_WRITE;

  if (*select_type == SELECT_READ)
    {
      pthread_mutex_lock (&rpipe->lock);
      err = pipe_select_readable (rpipe, tsp, data_only);
      pthread_mutex_unlock (&rpipe->lock);
    }
  else if (*select_type == SELECT_WRITE)
    {
      pthread_mutex_lock (&wpipe->lock);
      err = pipe_select_writable (wpipe, tsp);
      pthread_mutex_unlock (&wpipe->lock);
    }
  else
    /* ugh */
    {
      int rpipe_blocked, wpipe_blocked;
      struct pipe_select_cond pending_select;
      size_t wlimit = wpipe->write_limit;
      pthread_mutex_t *lock =
	(wpipe == rpipe ? &rpipe->lock : &pipe_multiple_lock);

      pthread_cond_init (&pending_select.cond, NULL);

      pthread_mutex_lock (lock);
      if (rpipe == wpipe)
	pipe_add_select_cond (rpipe, &pending_select);
      else
	{
	  pthread_mutex_lock (&rpipe->lock);
	  pthread_mutex_lock (&wpipe->lock);
	  pipe_add_select_cond (rpipe, &pending_select);
	  pipe_add_select_cond (wpipe, &pending_select);
	}

      rpipe_blocked =
	! ((rpipe->flags & PIPE_BROKEN) || pipe_is_readable (rpipe, data_only));
      wpipe_blocked =
	! ((wpipe->flags & PIPE_BROKEN) || pipe_readable (wpipe, 1) < wlimit);
      while (!err && rpipe_blocked && wpipe_blocked)
	{
	  if (rpipe != wpipe)
	    {
	      pthread_mutex_unlock (&rpipe->lock);
	      pthread_mutex_unlock (&wpipe->lock);
	    }
	  err = pthread_hurd_cond_timedwait_np (&pending_select.cond, lock,
						tsp);
	  if (rpipe != wpipe)
	    {
	      pthread_mutex_lock (&rpipe->lock);
	      pthread_mutex_lock (&wpipe->lock);
	    }
	  rpipe_blocked =
	    ! ((rpipe->flags & PIPE_BROKEN)
	       || pipe_is_readable (rpipe, data_only));
	  wpipe_blocked =
	    ! ((wpipe->flags & PIPE_BROKEN)
	       || pipe_readable (wpipe, 1) < wlimit);
	}

      if (!err)
	{
	  if (rpipe_blocked)
	    *select_type &= ~SELECT_READ;
	  if (wpipe_blocked)
	    *select_type &= ~SELECT_WRITE;
	}

      if (rpipe == wpipe)
	pipe_remove_select_cond (rpipe, &pending_select);
      else
	{
	  pipe_remove_select_cond (rpipe, &pending_select);
	  pipe_remove_select_cond (wpipe, &pending_select);
	  pthread_mutex_unlock (&rpipe->lock);
	  pthread_mutex_unlock (&wpipe->lock);
	}
      pthread_mutex_unlock (lock);
    }

  if (err == ETIMEDOUT)
    {
      err = 0;
      *select_type = 0;
    }

  return err;
}

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If present, the information in
   CONTROL & PORTS is written in a preceding control packet.  If an error is
   returned, nothing is done.  */
error_t
pipe_send (struct pipe *pipe, int noblock, void *source,
	   char *data, size_t data_len,
	   char *control, size_t control_len,
	   mach_port_t *ports, size_t num_ports,
	   size_t *amount)
{
  error_t err;

  /* Nothing to do.  */
  if (data_len == 0 && control_len == 0 && num_ports == 0)
    {
      *amount = 0;
      return 0;
    }

  err = pipe_wait_writable (pipe, noblock);
  if (err)
    return err;

  if (noblock)
    {
      size_t left = pipe->write_limit - pipe_readable (pipe, 1);
      if (left < data_len)
	{
	  if (data_len <= pipe->write_atomic)
	    return EWOULDBLOCK;
	  else
	    data_len = left;
	}
    }

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
	    {
	      /* Trash CONTROL_PACKET somehow XXX */
	    }
	}
    }

  if (!err)
    err = (*pipe->class->write)(pipe->queue, source, data, data_len, amount);

  if (!err)
    {
      timestamp (&pipe->write_time);

      /* And wakeup anyone that might be interested in it.  */
      pthread_cond_broadcast (&pipe->pending_reads);
      pthread_mutex_unlock (&pipe->lock);

      pthread_mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
      /* Only wakeup selects if there's still data available.  */
      if (pipe_is_readable (pipe, 0))
	{
	  pthread_cond_broadcast (&pipe->pending_read_selects);
	  pipe_select_cond_broadcast (pipe);
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

  err = pipe_wait_readable (pipe, noblock, data_only);
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
      assert_backtrace (packet);		/* pipe_write always writes a data packet.  */
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
    {
      if (packet)
	/* Read some data (PACKET must be a data packet at this point).  */
	{
	  int dq = 1;	/* True if we should dequeue this packet.  */

	  if (source)
	    packet_read_source (packet, source);

	  err = (*pipe->class->read)(packet, &dq, flags,
				     data, data_len, amount);
	  if (dq)
	    pq_dequeue (pq);
	}
      else
	/* Return EOF.  */
	*data_len = 0;
    }

  if (!err && packet)
    {
      timestamp (&pipe->read_time);

      /* And wakeup anyone that might be interested in it.  */
      pthread_cond_broadcast (&pipe->pending_writes);
      pthread_mutex_unlock (&pipe->lock);

      pthread_mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
      /* Only wakeup selects if there's still writing space available.  */
      if (pipe_readable (pipe, 1) < pipe->write_limit)
	{
	  pthread_cond_broadcast (&pipe->pending_write_selects);
	  pipe_select_cond_broadcast (pipe);
	  /* We leave PIPE locked here, assuming the caller will soon unlock
	     it and allow others access.  */
	}
    }

  return err;
}
