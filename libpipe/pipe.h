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

#ifndef __PIPE_H__
#define __PIPE_H__

#include <cthreads.h>		/* For conditions & mutexes */

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
struct pipe_class *stream_pipe_class;
struct pipe_class *dgram_pipe_class;
struct pipe_class *seqpacket_pipe_class;

/* A unidirectional data pipe; it transfers data from READER to WRITER.  */
struct pipe
{
  /* What kind of pipe we are.  */
  struct pipe_class *class;

  /* We use this to keep track of active threads using this pipe, so that
     while a thread is waiting to read from a pipe and that pipe gets
     deallocated (say by socket_shutdown), it doesn't actually go away until
     the reader realizes what happened.  It is normally frobbed using
     pipe_aquire & pipe_release, which do locking as well..  */
  unsigned refs;

  /* Various flags, from PIPE_* below.  */
  unsigned flags;

  /* Various timestamps for this pipe.  */
  time_value_t read_time;
  time_value_t write_time;

  struct condition pending_reads;
  struct condition pending_selects;

  struct mutex lock;

  /* When a pipe receives an interrupt, we want to wake up all pending read
     threads, and have them realize they've been interrupted; reads that
     happen after the interrupt shouldn't return EINTR.  When a thread waits
     on this pipe's PENDING_READS condition, it remembers this sequence
     number; any interrupt bumps this number and broadcasts on the condition.
     A reader thread will try to read from the pipe only if the sequence
     number is the same as when it went to sleep. */
  unsigned long interrupt_seq_num;

  /* A queue of incoming packets, of type either PACKET_TYPE_DATA or
     PACKET_TYPE_CONTROL.  Each data packet represents one datagram for
     protocols that maintain record boundaries.  Control packets always
     represent the control information to be returned from one read
     operation, and will be returned in conjuction with the following data
     packet (if any).  Reads interested only in data just skip control
     packets until they find a data packet.  */
  struct pq *queue;
};

/* Pipe flags.  */
#define PIPE_BROKEN	0x1	/* This pipe isn't connected.  */

/* Returns the number of characters quickly readable from PIPE.  If DATA_ONLY
   is true, then `control' packets are ignored.  */
extern inline size_t
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
extern inline int
pipe_is_readable (struct pipe *pipe, int data_only)
{
  struct pq *pq = pipe->queue;
  struct packet *packet = pq_head (pq, PACKET_TYPE_ANY, NULL);
  if (data_only)
    while (packet && packet->type == PACKET_TYPE_CONTROL)
      packet = packet->next;
  return (packet != NULL);
}

/* Waits for PIPE to be readable, or an error to occurr.  If NOBLOCK is true,
   this operation will return EWOULDBLOCK instead of blocking when no data is
   immediately available.  If DATA_ONLY is true, then `control' packets are
   ignored.  */
extern inline error_t
pipe_wait (struct pipe *pipe, int noblock, int data_only)
{
  while (! pipe_is_readable (pipe, data_only) && ! (pipe->flags & PIPE_BROKEN))
    {
      unsigned seq_num = pipe->interrupt_seq_num;
      if (noblock)
	return EWOULDBLOCK;
      condition_wait (&pipe->pending_reads, &pipe->lock);
      if (seq_num != pipe->interrupt_seq_num)
	return EINTR;
    }
  return 0;
}
 
/* Wake up all threads waiting on PIPE, which should be locked.  */
void pipe_kick (struct pipe *pipe);

/* Creates a new pipe of class CLASS and returns it in RESULT.  */
error_t pipe_create (struct pipe_class *class, struct pipe **pipe);

/* Free PIPE and any resources it holds.  */
void pipe_free (struct pipe *pipe);

/* Discard a reference to PIPE, which should be unlocked, being sure to make
   users aware of this.  */
void pipe_break (struct pipe *pipe);

/* Lock PIPE and increment its ref count.  */
extern inline void
pipe_aquire (struct pipe *pipe)
{
  mutex_lock (&pipe->lock);
  pipe->refs++;
}

/* Decrement PIPE's (which should be locked) ref count and unlock it.  If the
   ref count goes to zero, PIPE will be destroyed.  */
extern inline void
pipe_release (struct pipe *pipe)
{
  if (--pipe->refs == 0)
    pipe_free (pipe);
  else
    mutex_unlock (&pipe->lock);
}

/* Empty out PIPE of any data.  PIPE should be locked.  */
extern inline void
pipe_drain (struct pipe *pipe)
{
  pq_drain (pipe->queue);
}

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If present, the information in
   CONTROL & PORTS is written in a preceding control packet.  If an error is
   returned, nothing is done.  If non-NULL, SOURCE is recorded as the source
   of the data, to be provided to any readers of it; if no reader ever reads
   it, it's deallocated by calling pipe_dealloc_addr.  */
error_t pipe_send (struct pipe *pipe, void *source,
		   char *data, size_t data_len,
		   char *control, size_t control_len,
		   mach_port_t *ports, size_t num_ports,
		   size_t *amount);

/* Writes up to LEN bytes of DATA, to PIPE, which should be locked, and
   returns the amount written in AMOUNT.  If an error is returned, nothing is
   done.  If non-NULL, SOURCE is recorded as the source of the data, to be
   provided to any readers of it; if no reader ever reads it, it's
   deallocated by calling pipe_dealloc_addr.  */
#define pipe_write(pipe, source, data, data_len, amount) \
  pipe_send (pipe, source, data, data_len, 0, 0, 0, 0, amount)

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

/* ---------------------------------------------------------------- */
/* User-provided functions.  */

/* This routine may be provided by the user, in which case, it should be a
   function taking a non-NULL source address and deallocating it.  It
   defaults to calling ports_port_deref.  */
void pipe_dealloc_addr (void *addr);

#endif /* __PIPE_H__ */
