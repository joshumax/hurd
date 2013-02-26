/* Generic one-way pipes

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#ifndef __PIPE_H__
#define __PIPE_H__

#define EWOULDBLOCK EAGAIN /* XXX */

#include <pthread.h>		/* For conditions & mutexes */
#include <features.h>

#ifdef PIPE_DEFINE_EI
#define PIPE_EI
#else
#define PIPE_EI __extern_inline
#endif

#include "pq.h"


/* A description of a class of pipes and how to operate on them.  */
struct pipe_class
{
  /* What sort of socket this corresponds too.  */
  int sock_type;

  /* Flags, from PIPE_CLASS_*, below.  */
  unsigned flags;

  /* Operations: */
  /* Read from PACKET into DATA &c, and set *DEQUEUE to true if PACKET should
     be subsequently discarded.  */
  error_t (*read)(struct packet *packet, int *dequeue, unsigned *flags,
		  char **data, size_t *data_len, size_t amount);
  /* Write DATA &c into the packet queue PQ.  */
  error_t (*write)(struct pq *pq, void *source,
		   char *data, size_t data_len, size_t *amount);
};

/* pipe_class flags  */
#define PIPE_CLASS_CONNECTIONLESS	0x1 /* A non-stream protocol.  */

/* Some pre-defined pipe_classes.  */
extern struct pipe_class *stream_pipe_class;
extern struct pipe_class *dgram_pipe_class;
extern struct pipe_class *seqpack_pipe_class;

struct pipe_select_cond
{
  struct pipe_select_cond *next;
  struct pipe_select_cond *prev;
  pthread_cond_t cond;
};

/* A unidirectional data pipe; it transfers data from READER to WRITER.  */
struct pipe
{
  /* What kind of pipe we are.  */
  struct pipe_class *class;

  /* We use this to keep track of active threads using this pipe, so that
     while a thread is waiting to read from a pipe and that pipe gets
     deallocated (say by socket_shutdown), it doesn't actually go away until
     the reader realizes what happened.  It is normally frobbed using
     pipe_acquire & pipe_release, which do locking as well..  */
  unsigned readers, writers;

  /* Various flags, from PIPE_* below.  */
  unsigned flags;

  /* Various timestamps for this pipe.  */
  time_value_t read_time;
  time_value_t write_time;

  pthread_cond_t pending_reads;
  pthread_cond_t pending_read_selects;

  pthread_cond_t pending_writes;
  pthread_cond_t pending_write_selects;

  struct pipe_select_cond *pending_selects;

  /* The maximum number of characters that this pipe will hold without
     further writes blocking.  */
  size_t write_limit;

  /* Write requests of less than this much are always done atomically.  */
  size_t write_atomic;

  pthread_mutex_t lock;

  /* A queue of incoming packets, of type either PACKET_TYPE_DATA or
     PACKET_TYPE_CONTROL.  Each data packet represents one datagram for
     protocols that maintain record boundaries.  Control packets always
     represent the control information to be returned from one read
     operation, and will be returned in conjunction with the following data
     packet (if any).  Reads interested only in data just skip control
     packets until they find a data packet.  */
  struct pq *queue;
};

/* Pipe flags.  */
#define PIPE_BROKEN	0x1	/* This pipe isn't connected.  */


extern size_t pipe_readable (struct pipe *pipe, int data_only);

extern int pipe_is_readable (struct pipe *pipe, int data_only);

extern error_t pipe_wait_readable (struct pipe *pipe, int noblock, int data_only);

extern error_t pipe_select_readable (struct pipe *pipe, struct timespec *tsp,
				     int data_only);

extern error_t pipe_wait_writable (struct pipe *pipe, int noblock);

extern error_t pipe_select_writable (struct pipe *pipe, struct timespec *tsp);

#if defined(__USE_EXTERN_INLINES) || defined(PIPE_DEFINE_EI)

/* Returns the number of characters quickly readable from PIPE.  If DATA_ONLY
   is true, then `control' packets are ignored.  */
PIPE_EI size_t
pipe_readable (struct pipe *pipe, int data_only)
{
  size_t readable = 0;
  struct pq *pq = pipe->queue;
  struct packet *packet = pq_head (pq, PACKET_TYPE_ANY, NULL);
  while (packet)
    {
      if (packet->type == PACKET_TYPE_DATA)
	readable += packet_readable (packet);
      packet = packet->next;
    }
  return readable;
}

/* Returns true if there's any data available in PIPE.  If DATA_ONLY is true,
   then `control' packets are ignored.  Note that this is different than
   (pipe_readable (PIPE) > 0) in the case where a control packet containing
   only ports is present.  */
PIPE_EI int
pipe_is_readable (struct pipe *pipe, int data_only)
{
  struct pq *pq = pipe->queue;
  struct packet *packet = pq_head (pq, PACKET_TYPE_ANY, NULL);
  if (data_only)
    while (packet && packet->type == PACKET_TYPE_CONTROL)
      packet = packet->next;
  return (packet != NULL);
}

/* Waits for PIPE to be readable, or an error to occur.  If NOBLOCK is true,
   this operation will return EWOULDBLOCK instead of blocking when no data is
   immediately available.  If DATA_ONLY is true, then `control' packets are
   ignored.  */
PIPE_EI error_t
pipe_wait_readable (struct pipe *pipe, int noblock, int data_only)
{
  while (! pipe_is_readable (pipe, data_only) && ! (pipe->flags & PIPE_BROKEN))
    {
      if (noblock)
	return EWOULDBLOCK;
      if (pthread_hurd_cond_wait_np (&pipe->pending_reads, &pipe->lock))
	return EINTR;
    }
  return 0;
}

/* Waits for PIPE to be readable, or an error to occur.  This call only
   returns once threads waiting using pipe_wait_readable have been woken and
   given a chance to read, and if there is still data available thereafter.
   If DATA_ONLY is true, then `control' packets are ignored.  */
PIPE_EI error_t
pipe_select_readable (struct pipe *pipe, struct timespec *tsp, int data_only)
{
  error_t err = 0;
  while (! pipe_is_readable (pipe, data_only) && ! (pipe->flags & PIPE_BROKEN))
    {
      err = pthread_hurd_cond_timedwait_np (&pipe->pending_read_selects,
					    &pipe->lock, tsp);
      if (err)
	break;
    }
  return err;
}

/* Block until data can be written to PIPE.  If NOBLOCK is true, then
   EWOULDBLOCK is returned instead of blocking if this can't be done
   immediately.  */
PIPE_EI error_t
pipe_wait_writable (struct pipe *pipe, int noblock)
{
  size_t limit = pipe->write_limit;
  if (pipe->flags & PIPE_BROKEN)
    return EPIPE;
  while (pipe_readable (pipe, 1) >= limit)
    {
      if (noblock)
	return EWOULDBLOCK;
      if (pthread_hurd_cond_wait_np (&pipe->pending_writes, &pipe->lock))
	return EINTR;
      if (pipe->flags & PIPE_BROKEN)
	return EPIPE;
    }
  return 0;
}

/* Block until some data can be written to PIPE.  This call only returns once
   threads waiting using pipe_wait_writable have been woken and given a
   chance to write, and if there is still space available thereafter.  */
PIPE_EI error_t
pipe_select_writable (struct pipe *pipe, struct timespec *tsp)
{
  size_t limit = pipe->write_limit;
  error_t err = 0;
  while (! (pipe->flags & PIPE_BROKEN) && pipe_readable (pipe, 1) >= limit)
    {
      err = pthread_hurd_cond_timedwait_np (&pipe->pending_writes,
					    &pipe->lock, tsp);
      if (err)
	break;
    }
  return err;
}

#endif /* Use extern inlines.  */

/* Creates a new pipe of class CLASS and returns it in RESULT.  */
error_t pipe_create (struct pipe_class *class, struct pipe **pipe);

/* Free PIPE and any resources it holds.  */
void pipe_free (struct pipe *pipe);

/* Take any actions necessary when PIPE acquires its first reader.  */ 
void _pipe_first_reader (struct pipe *pipe);

/* Take any actions necessary when PIPE acquires its first writer.  */ 
void _pipe_first_writer (struct pipe *pipe);

/* Take any actions necessary when PIPE's last reader has gone away.  PIPE
   should be locked.  */
void _pipe_no_readers (struct pipe *pipe);

/* Take any actions necessary when PIPE's last writer has gone away.  PIPE
   should be locked.  */
void _pipe_no_writers (struct pipe *pipe);

extern void pipe_acquire_reader (struct pipe *pipe);

extern void pipe_acquire_writer (struct pipe *pipe);

extern void pipe_release_reader (struct pipe *pipe);

extern void pipe_release_writer (struct pipe *pipe);

extern void pipe_add_reader (struct pipe *pipe);

extern void pipe_add_writer (struct pipe *pipe);

extern void pipe_remove_reader (struct pipe *pipe);

extern void pipe_remove_writer (struct pipe *pipe);

extern void pipe_drain (struct pipe *pipe);

#if defined(__USE_EXTERN_INLINES) || defined(PIPE_DEFINE_EI)

/* Lock PIPE and increment its readers count.  */
PIPE_EI void
pipe_acquire_reader (struct pipe *pipe)
{
  pthread_mutex_lock (&pipe->lock);
  if (pipe->readers++ == 0)
    _pipe_first_reader (pipe);
}

/* Lock PIPE and increment its writers count.  */
PIPE_EI void
pipe_acquire_writer (struct pipe *pipe)
{
  pthread_mutex_lock (&pipe->lock);
  if (pipe->writers++ == 0)
    _pipe_first_writer (pipe);
}

/* Decrement PIPE's (which should be locked) reader count and unlock it.  If
   there are no more refs to PIPE, it will be destroyed.  */
PIPE_EI void
pipe_release_reader (struct pipe *pipe)
{
  if (--pipe->readers == 0)
    _pipe_no_readers (pipe);
  else
    pthread_mutex_unlock (&pipe->lock);
}

/* Decrement PIPE's (which should be locked) writer count and unlock it.  If
   there are no more refs to PIPE, it will be destroyed.  */
PIPE_EI void
pipe_release_writer (struct pipe *pipe)
{
  if (--pipe->writers == 0)
    _pipe_no_writers (pipe);
  else
    pthread_mutex_unlock (&pipe->lock);
}

/* Increment PIPE's reader count.  PIPE should be unlocked.  */
PIPE_EI void
pipe_add_reader (struct pipe *pipe)
{
  pipe_acquire_reader (pipe);
  pthread_mutex_unlock (&pipe->lock);
}

/* Increment PIPE's writer count.  PIPE should be unlocked.  */
PIPE_EI void
pipe_add_writer (struct pipe *pipe)
{
  pipe_acquire_writer (pipe);
  pthread_mutex_unlock (&pipe->lock);
}

/* Decrement PIPE's (which should be unlocked) reader count and unlock it.  If
   there are no more refs to PIPE, it will be destroyed.  */
PIPE_EI void
pipe_remove_reader (struct pipe *pipe)
{
  pthread_mutex_lock (&pipe->lock);
  pipe_release_reader (pipe);
}

/* Decrement PIPE's (which should be unlocked) writer count and unlock it.  If
   there are no more refs to PIPE, it will be destroyed.  */
PIPE_EI void
pipe_remove_writer (struct pipe *pipe)
{
  pthread_mutex_lock (&pipe->lock);
  pipe_release_writer (pipe);
}

/* Empty out PIPE of any data.  PIPE should be locked.  */
PIPE_EI void
pipe_drain (struct pipe *pipe)
{
  pq_drain (pipe->queue);
}

#endif /* Use extern inlines.  */

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If present, the information in
   CONTROL & PORTS is written in a preceding control packet.  If an error is
   returned, nothing is done.  If non-NULL, SOURCE is recorded as the source
   of the data, to be provided to any readers of it; if no reader ever reads
   it, it's deallocated by calling pipe_dealloc_addr.  */
error_t pipe_send (struct pipe *pipe, int noblock, void *source,
		   char *data, size_t data_len,
		   char *control, size_t control_len,
		   mach_port_t *ports, size_t num_ports,
		   size_t *amount);

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If an error is returned, nothing is
   done.  If non-NULL, SOURCE is recorded as the source of the data, to be
   provided to any readers of it; if no reader ever reads it, it's
   deallocated by calling pipe_dealloc_addr.  */
#define pipe_write(pipe, noblock, source, data, data_len, amount) \
  pipe_send (pipe, noblock, source, data, data_len, 0, 0, 0, 0, amount)

/* Reads up to AMOUNT bytes from PIPE, which should be locked, into DATA, and
   returns the amount read in DATA_LEN.  If NOBLOCK is true, EWOULDBLOCK is
   returned instead of block when no data is immediately available.  If an
   error is returned, nothing is done.  If source isn't NULL, the
   corresponding source provided by the sender is returned in it; this may be
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
error_t pipe_recv (struct pipe *pipe, int noblock, unsigned *flags,
		   void **source,
		   char **data, size_t *data_len, size_t amount,
		   char **control, size_t *control_len,
		   mach_port_t **ports, size_t *num_ports);


/* Reads up to AMOUNT bytes from PIPE, which should be locked, into DATA, and
   returns the amount read in DATA_LEN.  If NOBLOCK is true, EWOULDBLOCK is
   returned instead of block when no data is immediately available.  If an
   error is returned, nothing is done.  If source isn't NULL, the
   corresponding source provided by the sender is returned in it; this may be
   NULL if it wasn't specified by the sender (which is usually the case with
   connection-oriented protcols).  */
#define pipe_read(pipe, noblock, source, data, data_len, amount) \
  pipe_recv (pipe, noblock, 0, source, data, data_len, amount, 0,0,0,0)

/* Hold this lock before attempting to lock multiple pipes. */
extern pthread_mutex_t pipe_multiple_lock;

/* Return when either RPIPE is available for reading (if SELECT_READ is set
   in *SELECT_TYPE), or WPIPE is available for writing (if select_write is
   set in *SELECT_TYPE).  *SELECT_TYPE is modified to reflect which (or both)
   is now available.  DATA_ONLY should be true if only data packets should be
   waited for on RPIPE.  Neither RPIPE or WPIPE should be locked when calling
   this function (unlike most pipe functions).  */
error_t pipe_pair_select (struct pipe *rpipe, struct pipe *wpipe,
			  struct timespec *tsp, int *select_type,
			  int data_only);

/* ---------------------------------------------------------------- */
/* User-provided functions.  */

/* This routine may be provided by the user, in which case, it should be a
   function taking a non-NULL source address and deallocating it.  It
   defaults to calling ports_port_deref.  */
void pipe_dealloc_addr (void *addr);

#endif /* __PIPE_H__ */
