/* A server for local sockets, of type PF_LOCAL

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

#ifndef __PFLOCAL_H__
#define __PFLOCAL_H__

/* ---------------------------------------------------------------- */
/* Sockets */

/* A port on SOCK.  Multiple sock_user's can point to the same socket.  */ 
struct sock_user
{
  struct port_info pi;
  struct sock *sock;
};

/* An endpoint for a possible I/O stream.  */
struct sock
{
  int refs;
  struct mutex lock;

  /* Reads from this socket come from READ_PIPE, writes go to WRITE_PIPE.
     A sock always has a read pipe, and a write pipe when it's connected to
     another socket.  */
  struct pipe *read_pipe, *write_pipe;

  /* FLAGS from SOCK_*, below.  */
  unsigned flags;

  /* An identifying number for the socket.  */
  unsigned id;
  /* Last time the socket got frobbed.  */
  time_value_t change_time;

  /* This socket's local address.  Note that we don't hold any references on
     ADDR, and depend on the addr zeroing our pointer if it goes away (which
     is ok, as we can then just make up another address if necessary, and no
     one could tell anyway).  */
  struct addr *addr;

  /* If this sock has been connected to another sock, then WRITE_ADDR is the
     addr of that sock.  We *do* hold a reference to this addr.  */
  struct addr *write_addr;

  /* A connection queue to listen for incoming connections on.  Once a socket
     has one of these, it always does, and can never again be used for
     anything but accepting incoming connections.  */
  struct connq *connq;
};

/* Socket flags */
#define SOCK_CONNECTED		0x1 /* A connected connection-oriented sock. */
#define SOCK_NONBLOCK		0x2 /* Don't block on I/O.  */
#define SOCK_SHUTDOWN_READ	0x4 /* The read-half has been shutdown.  */
#define SOCK_SHUTDOWN_WRITE	0x8 /* The write-half has been shutdown.  */

/* Returns the pipe that SOCK is reading from in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  NULL may
   also be returned in PIPE with a 0 error, meaning that EOF should be
   returned.  SOCK mustn't be locked.  */
error_t sock_aquire_read_pipe (struct sock *sock, struct pipe **pipe);

/* Returns the pipe that SOCK is writing to in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t sock_aquire_write_pipe (struct sock *sock, struct pipe **pipe);

/* Connect together the previously unconnected sockets SOCK1 and SOCK2.  */
error_t sock_connect (struct sock *sock1, struct sock *sock2)

/* Return a new socket with the given pipe class in SOCK.  */
error_t sock_create (struct pipe_class *pipe_class, struct sock **sock);

/* Return a new socket just like TEMPLATE in SOCK.  */
error_t sock_create (struct sock *template, struct sock **sock);

/* Return a new user port on SOCK in PORT.  */
error_t sock_create_port (struct sock *sock, mach_port_t *port);

/* ---------------------------------------------------------------- */
/* Pipes */

/* A description of a class of pipes and how to operate on them.  */
struct pipe_class
{
  /* What sort of socket this corresponds too.  */
  int sock_type;

  /* Flags, from PIPE_CLASS_*, below.  */
  unsigned flags;

  /* Operations: */

  /* Read from BUF into DATA &c.  */
  error_t (*read)(void *buf, char **data, unsigned *data_len, unsigned amount);
  /* Write DATA &c into BUF.  */
  error_t (*write)(void *buf, char *data, unsigned data_len, unsigned *amount);
  /* Return the number of readable characters in BUF.  */
  unsigned (*readable)(void *buf);
  /* Clear out any data.  */
  error_t (*drain)(void *buf);
  /* Initialize *BUF or return an error.  */
  error_t (*init)(void **buf);
  /* Cleanup BUF, freeing all resources.  */
  void (*cleanup)(void *buf);
};

/* pipe_class flags  */
#define PIPE_CLASS_CONNECTIONLESS	0x1 /* A non-stream protocol.  */

/* Some pre-defined pipe_classes.  */
struct pipe_class *stream_pipe_class;
struct pipe_class *dgram_pipe_class;
struct pipe_class *seqpacket_pipe_class;

/* ---------------------------------------------------------------- */

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

  /* When interrupt_operation is called on a socket, we want to wake up all
     pending read threads, and have them realize they've been interrupted;
     reads that happen after the interrupt shouldn't return EINTR.  When a
     thread waits on this pipe's PENDING_READS condition, it remembers this
     sequence number; any interrupt bumps this number and broadcasts on the
     condition.  A reader thread will try to read from the pipe only if the
     sequence number is the same as when it went to sleep. */
  unsigned long interrupt_seq_num;

  /* The actual data, and how to access it.  */
  void *data;
};

/* Pipe flags.  */
#define PIPE_BROKEN	0x1	/* This pipe isn't connected.  */

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

/* Returns the number of characters quickly readable from PIPE.  */
extern inline unsigned
pipe_readable (struct pipe *pipe)
{
  return (*pipe->class->readable)(pipe->data);
}

/* Empty out PIPE of any data.  */
extern inline void
pipe_drain (struct pipe *pipe)
{
  (*pipe->class->drain)(pipe->data);
}

/* Writes up to LEN bytes of DATA, to PIPE, and returns the amount written in
   AMOUNT.  If an error is returned, nothing is done.  */
error_t
pipe_write (struct pipe *pipe, char *data, unsigned len, unsigned *amount);

/* Reads up to AMOUNT bytes from PIPE, which should be locked, into DATA, and
   returns the amount written in LEN.  If NOBLOCK is true, EWOULDBLOCK is
   returned instead of block when no data is immediately available.  If an
   error is returned, nothing is done.  */
error_t pipe_read (struct pipe *pipe, int noblock,
		   char **data, unsigned *len, unsigned amount);

/* Waits for PIPE to be readable, or an error to occurr.  If NOBLOCK is true,
   this operation will return EWOULDBLOCK instead of blocking when no data is
   immediately available.  */
error_t pipe_wait (struct pipe *pipe, int noblock);
 
/* Wake up all threads waiting on PIPE, which should be locked.  */
void pipe_kick (struct pipe *pipe);

/* Creates a new pipe of class CLASS and returns it in RESULT.  */
error_t pipe_create (struct pipe_class *class, struct pipe **pipe);

/* Free PIPE and any resources it holds.  */
void pipe_free (struct pipe *pipe);

/* ---------------------------------------------------------------- */

struct connq;
struct connq_request;

/* Create a new listening queue, returning it in CQ.  The resulting queue
   will be of zero length, that is it won't allow connections unless someone
   is already listening (change this with connq_set_length).  */
error_t connq_create (struct connq **cq);

/* Wait for a connection attempt to be made on CQ, and return the connecting
   socket in SOCK, and a request tag in REQ.  If REQ is NULL, the request is
   left in the queue, otherwise connq_request_complete must be called on REQ
   to allow the requesting thread to continue.  If NOBLOCK is true,
   EWOULDBLOCK is returned when there are no immediate connections
   available. */
error_t connq_listen (struct connq *cq, int noblock,
		      struct connq_request **req, struct sock **sock);

/* Return the error code ERR to the thread that made the listen request REQ,
   returned from a previous connq_listen.  */
void connq_request_complete (struct connq_request *req, error_t err);

/* Set CQ's queue length to LENGTH.  Any sockets already waiting for a
   connections that are past the new length will fail with ECONNREFUSED.  */
error_t connq_set_length (struct connq *cq, int length);

/* Try to connect SOCK with the socket listening on CQ.  If NOBLOCK is true,
   then return EWOULDBLOCK immediately when there are no immediate
   connections available. */
error_t connq_connect (struct connq *cq, int noblock, struct sock *sock);

/* ---------------------------------------------------------------- */
/* Addresses */

struct addr
{
  struct port_info pi;
  struct mutex lock;

  /* Which socket is at this address. */
  struct sock *sock;
};

/* Create a new address.  */
error_t addr_create (struct addr **addr);

/* Set ADDR's socket to SOCK, possibly discarding a previous binding.  */
error_t addr_set_sock (struct addr *addr, struct sock *sock);

/* ---------------------------------------------------------------- */

extern inline void
timestamp (time_value_t *stamp)
{
  host_get_time (mach_host_self (), stamp);
}

#endif /* __PFLOCAL_H__ */
