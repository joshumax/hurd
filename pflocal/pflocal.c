/* A server for local sockets, of type PF_LOCAL.

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

/* ---------------------------------------------------------------- */

/* A bidirectional endpoint for local pipes.  */
struct socket
{
  int refs;
  struct mutex lock;

  /* Reads from this socket come from READ_PIPE, writes go to WRITE_PIPE.  */
  struct pipe *read_pipe, *write_pipe;

  /* An identifying number for the socket.  */
  unsigned id;
  /* Last time the socket got frobbed.  */
  time_value_t change_time;
};

/* A port on SOCKET.  Multiple socket_user's can point to the same socket.  */
struct socket_user
{
  struct port_info pi;
  struct socket *socket;
};

/* A unidirectional data pipe; it transfers data from READER to WRITER.  */
struct pipe
{
  /* We use this to keep track of active threads using this pipe, so that
     while a thread is waiting to read from a pipe and that pipe gets
     deallocated (say by socket_shutdown), it doesn't actually go away until
     the reader realizes what happened.  It is normally frobbed using
     pipe_aquire & pipe_release.  */
  unsigned refs;

  /* The endpoints of this socket.  */
  struct socket *reader, *writer;

  /* Various timestamps for this pipe.  */
  time_value_t read_time;
  time_value_t write_time;

  struct condition pending_reads;
  struct condition pending_selects;

  struct mutex lock;

  /* When interrupt_operation is called on a socket, we want to wake up all
     pending read threads, and have them realize they've been interrupted.
     Reads that happen after the interrupt shouldn't return EINTR.  So when a
     thread waits on this pipes PENDING_READS condition, it remembers this
     sequence number; any interrupt bumps this number and broadcasts on the
     condition.  A reader thread will try to read from the pipe only if the
     sequence number is the same as when it went to sleep. */
  unsigned long interrupt_seq_num;

  /* The actual data.  */
  char *buffer;
  unsigned alloced;
  char *start, *end;
};

/* ---------------------------------------------------------------- */

/* Returns true if PIPE contains data.  PIPE must be locked.  */
static inline int
pipe_readable (struct pipe *pipe)
{
  return pipe->start < pipe->end;
}

/* Lock PIPE and increment its ref count.  */
static inline void
pipe_aquire (struct pipe *pipe)
{
  mutex_lock (&pipe->lock);
  pipe->refs++;
}

/* Decrement PIPEs (which should be locked) ref count and unlock it.  */
static inline void
pipe_release (struct pipe *pipe)
{
  pipe->refs--;
  mutex_unlock (&pipe->lock);
}

/* Returns the pipe that SOCKET is reading from, locked and with an
   additional reference, or NULL if it has none.  */
struct pipe *
socket_aquire_read_pipe (struct socket *socket)
{
  struct pipe *pipe;

  mutex_lock (&socket->lock);
  pipe = user->socket->read_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the socket!  */
  mutex_unlock (&socket->lock);

  return pipe;
}

/* Returns the pipe that SOCKET is writing from, locked and with an
   additional reference, or NULL if it has none.  */
struct pipe *
socket_aquire_write_pipe (struct socket *socket)
{
  struct pipe *pipe;

  mutex_lock (&socket->lock);
  pipe = user->socket->write_pipe;
  if (pipe != NULL)
    pipe_aquire (pipe);		/* Do this before unlocking the socket!  */
  mutex_unlock (&socket->lock);

  return pipe;
}

/* ---------------------------------------------------------------- */

static inline void
timestamp (time_value_t *stamp)
{
  host_get_time (mach_host_self (), stamp);
}

/* ---------------------------------------------------------------- */
/* PF socket ops.  */

S_socket_create (struct trivfs_protid *cred,
		 int sock_type, int protocol,
		 mach_port_t *socket_port,
		 mach_msg_type_name_t *socket_port_type)
{
  
}

/* ---------------------------------------------------------------- */
/* Socket I/O ops.  */

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in amount.  */
error_t
S_io_read (struct socket_user *user,
	   char **data, mach_msg_type_number_t *data_len,
	   off_t offset, mach_msg_type_number_t amount)
{
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  pipe = socket_aquire_read_pipe (user->socket);
  if (pipe == NULL)
    return EBADF;
  
  while (! pipe_readable (pipe) && pipe->writer)
    {
      unsigned seq_num = pipe->interrupt_seq_num;
      condition_wait (&pipe->pending_reads, &pipe->lock);
      if (seq_num != pipe->interrupt_seq_num)
	{
	  pipe_release (pipe);
	  return EINTR;
	}
    }

  if (amount > pipe->end - pipe->start)
    amount = pipe->end - pipe->start;

  if (amount > 0)
    {
      if (*datalen < amount)
	vm_allocate (mach_task_self (), (vm_address_t *)data, amount, 1);
      *datalen = amount;
      bcopy (pipe->start, *data, amount);
      pipe->start += amount;
      timestamp (&pipe->read_time);
    }

  pipe_release (pipe);
  return 0;
}

/* Cause a pending request on this object to immediately return.  The
   exact semantics are dependent on the specific object.  */
error_t
S_interrupt_operation (struct socket_user *user)
{
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  /* Interrupt pending reads on this socket.  */
  pipe = socket_aquire_read_pipe (user->socket);
  if (pipe != NULL)
    {
      /* Indicate to currently waiting threads they've been interrupted.  */
      pipe->interrupt_seq_num++;

      /* Now wake them all up for the bad news... */
      condition_broadcast (&pipe->pending_reads, &pipe->lock);
      mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
      condition_broadcast (&pipe->pending_selects, &pipe->lock);
      mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */

      pipe_release (pipe);
    }

  return 0;
}

S_io_get_openmodes (struct socket_user *user, int *bits)
{
  if (!user)
    return EOPNOTSUPP;
  *bits =
    (user->socket->read_pipe ? O_READ : 0)
      | (user->socket->write_pipe ? O_WRITE : 0);
  return 0;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
error_t 
S_io_readable (struct socket_user *user, mach_msg_type_number_t *amount)
{
  error_t err = 0;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&user->socket->lock);
  if (user->socket->read_pipe)
    *amount = user->socket->read_pipe->end - user->socket->read_pipe->start;
  else
    err = EBADF;
  mutex_unlock (&user->socket->lock);

  return err;
}

/* Change current read/write offset */
error_t
S_io_seek (struct socket_user *user,
	   off_t offset, int whence, off_t *new_offset)
{
  return user ? ESPIPE : EOPNOTSUPP;
}

/* Return a new port with the same semantics as the existing port. */
error_t
S_io_duplicate (struct socket_user *user,
		mach_port_t *new_port, mach_msg_type_name_t *new_port_type)
{
  struct socket *socket;
  struct socket_user *new_user;

  if (!user)
    return EOPNOTSUPP;

  socket = user->socket;
  mutex_lock (&socket->lock);
  socket->refs++;
  mutex_unlock (&socket->lock);

  new_user =
    port_allocate_port (socket_user_bucket,
			sizeof (struct socket_user),
			socket_user_class);
  new_user->socket = socket;

  *new_port = ports_get_right (new_user);
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

/* Write data to an IO object.  If offset is -1, write at the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount successfully written is returned in amount.  A
   given user should not have more than one outstanding io_write on an
   object at a time; servers implement congestion control by delaying
   responses to io_write.  Servers may drop data (returning ENOBUFS)
   if they recevie more than one write when not prepared for it.  */
error_t
S_io_write (struct socket_user *user,
	    char *data, mach_msg_type_number_t data_len,
	    off_t offset, mach_msg_type_number_t *amount)
{
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  pipe = socket_aquire_write_pipe (user->socket);
  if (pipe == NULL)
    return EBADF;
  
  if (pipe->reader == NULL)
    {
      pipe_release (pipe);
      return EPIPE;
    }

  if (pipe->buffer + pipe->alloced - pipe->end < data_len)
    /* Not enough room in the buffer for the additional data, so grow it.  */
    {
      int pipe_amount = pipe->end - pipe->start;

      pipe->alloced = pipe_amount + data_len;

      if (pipe->start != pipe->buffer)
	/* There is free space at the front of the buffer.  Get rid of it.  */
	{
	  char *new_buffer = malloc (pipe->alloced);
	  bcopy (pipe->start, new_buffer, pipe_amount);
	  free (pipe->buffer);
	  pipe->buffer = new_buffer;
	}
      else
	pipe->buffer = realloc (pipe->buffer, pipe->alloced);

      /* Now the data is guaranteed to start at the beginning of the buffer. */
      pipe->start = pipe->buffer;
      pipe->end = pipe->start + pipe_amount;
    }

  /* Add the new data.  */
  assert (pipe->buffer + pipe->alloced - pipe->end >= data_len);
  bcopy (data, pipe->end, data_len);
  pipe->end += data_len;
  *amount = data_len;
  timestamp (&pipe->read_time);

  /* And wakeup anyone that might be interested in it.  */
  condition_signal (&pipe->pending_reads, &pipe->lock);
  mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */

  /* Only wakeup selects if there's still data available.  */
  if (pipe->start < start->end)
    {
      condition_signal (&pipe->pending_selects, &pipe->lock);
      mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
    }

  pipe_release (pipe);
  return 0;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
error_t
S_io_select (struct socket_user *user, int *select_type, int *id_tag)
{
  struct socket *socket;

  if (!user)
    return EOPNOTSUPP;

  socket = user->socket;
  mutex_lock (&socket->lock);

  *select_type |= ~SELECT_URG;

  if ((*select_type & SELECT_WRITE) && !socket->write_pipe)
    {
      mutex_unlock (&socket->lock);
      return EBADF;
    }
  /* Otherwise, pipes are always writable... */

  if (*select_type & SELECT_READ)
    {
      struct pipe *pipe = socket->read_pipe;
      if (pipe)
	pipe_aquire (pipe);

      /* We unlock SOCKET here, as it's not subsequently used, and we might
	 go to sleep waiting for readable data.  */
      mutex_unlock (&socket->lock);

      if (!pipe)
	return EBADF;

      if (! pipe_readable (pipe))
	/* Nothing to read on PIPE yet...  */
	if (*select_type & ~SELECT_READ)
	  /* But there's other stuff to report, so return that.  */
	  *select_type &= ~SELECT_READ;
	else
	  /* The user only cares about reading, so wait until something is
	     readable.  */
	  while (! pipe_readable (pipe) && pipe->writer)
	    {
	      unsigned seq_num = pipe->interrupt_seq_num;
	      condition_wait (&pipe->pending_reads, &pipe->lock);
	      if (seq_num != pipe->interrupt_seq_num)
		{
		  pipe_release (pipe);
		  return EINTR;
		}
	    }

      pipe_release (pipe);
    }
  else
    mutex_unlock (&socket->lock);

  return 0;
}

/* Return the current status of the object.  Not all the fields of the
   io_statuf_t are meaningful for all objects; however, the access and
   modify times, the optimal IO size, and the fs type are meaningful
   for all objects.  */
error_t
S_io_stat (struct socket_user *user, struct stat *st)
{
  struct socket *socket;
  void copy_time (time_value_t from, time_t *to_sec, unsigned long *to_usec)
    {
      *to_sec = from.seconds;
      *to_usec = from.microseconds;
    }

  if (!user)
    return EOPNOTSUPP;

  socket = user->socket;

  bzero (st, sizeof (struct stat));

  st->st_fstype = FSTYPE_SOCKET;
  st->st_fsid = getpid ();
  st->st_ino = socket->id;

  st->st_blksize = vm_page_size * 8;

  mutex_lock (&socket->lock);	/* Make sure the pipes don't go away...  */

  if (socket->read_pipe)
    copy_time (&socket->read_pipe->read_time, &st->st_atime, &st->atime_usec);
  if (socket->write_pipe)
    copy_time (&socket->read_pipe->write_time, &st->st_mtime, &st->mtime_usec);
  copy_time (&socket->change_time, &st->st_ctime, &st->ctime_usec);

  return 0;
}

/* ---------------------------------------------------------------- */

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = 0;
