/* Socket I/O operations

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

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in amount.  */
error_t
S_io_read (struct sock_user *user,
	   char **data, mach_msg_type_number_t *data_len,
	   off_t offset, mach_msg_type_number_t amount)
{
  error_t err = 0;
  unsigned readable;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  pipe = sock_aquire_read_pipe (user->sock);
  if (pipe == NULL)
    return EBADF;
  
  while ((readable = pipe_readable (pipe)) == 0 && pipe->writer)
    {
      unsigned seq_num = pipe->interrupt_seq_num;
      condition_wait (&pipe->pending_reads, &pipe->lock);
      if (seq_num != pipe->interrupt_seq_num)
	{
	  pipe_release (pipe);
	  return EINTR;
	}
    }

  if (readable)
    err = pipe_read (pipe, data, data_len, amount);
  if (readable && !err)
    timestamp (&pipe->read_time);

  pipe_release (pipe);
  return err;
}

/* Cause a pending request on this object to immediately return.  The
   exact semantics are dependent on the specific object.  */
error_t
S_interrupt_operation (struct sock_user *user)
{
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  /* Interrupt pending reads on this socket.  We don't bother with writes
     since they never block.  */
  pipe = sock_aquire_read_pipe (user->sock);
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

S_io_get_openmodes (struct sock_user *user, int *bits)
{
  if (!user)
    return EOPNOTSUPP;
  *bits =
    (user->sock->read_pipe ? O_READ : 0)
      | (user->sock->write_pipe ? O_WRITE : 0);
  return 0;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
error_t 
S_io_readable (struct sock_user *user, mach_msg_type_number_t *amount)
{
  error_t err = 0;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&user->sock->lock);
  if (user->sock->read_pipe)
    *amount = pipe_readable (user->sock->read_pipe);
  else
    err = EBADF;
  mutex_unlock (&user->sock->lock);

  return err;
}

/* Change current read/write offset */
error_t
S_io_seek (struct sock_user *user,
	   off_t offset, int whence, off_t *new_offset)
{
  return user ? ESPIPE : EOPNOTSUPP;
}

/* Return a new port with the same semantics as the existing port. */
error_t
S_io_duplicate (struct sock_user *user,
		mach_port_t *new_port, mach_msg_type_name_t *new_port_type)
{
  struct sock *sock;
  struct sock_user *new_user;

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;
  mutex_lock (&sock->lock);
  sock->refs++;
  mutex_unlock (&sock->lock);

  new_user =
    port_allocate_port (sock_user_bucket,
			sizeof (struct sock_user),
			sock_user_class);
  new_user->sock = sock;

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
S_io_write (struct sock_user *user,
	    char *data, mach_msg_type_number_t data_len,
	    off_t offset, mach_msg_type_number_t *amount)
{
  error_t err = 0;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  pipe = sock_aquire_write_pipe (user->sock);
  if (pipe == NULL)
    return EBADF;
  
  if (pipe->reader == NULL)
    err = EPIPE;
  if (!err)
    err = pipe_write(pipe, data, data_len, amount);
  if (!err)
    {
      timestamp (&pipe->write_time);
      
      /* And wakeup anyone that might be interested in it.  */
      condition_signal (&pipe->pending_reads, &pipe->lock);
      mutex_lock (&pipe->lock);	/* Get back the lock on PIPE.  */
      
      /* Only wakeup selects if there's still data available.  */
      if (pipe_readable (pipe))
	{
	  condition_signal (&pipe->pending_selects, &pipe->lock);
	  mutex_lock (&pipe->lock); /* Get back the lock on PIPE.  */
	}
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
S_io_select (struct sock_user *user, int *select_type, int *id_tag)
{
  struct sock *sock;

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;
  mutex_lock (&sock->lock);

  *select_type |= ~SELECT_URG;

  if ((*select_type & SELECT_WRITE) && !sock->write_pipe)
    {
      mutex_unlock (&sock->lock);
      return EBADF;
    }
  /* Otherwise, pipes are always writable... */

  if (*select_type & SELECT_READ)
    {
      struct pipe *pipe = sock->read_pipe;
      if (pipe)
	pipe_aquire (pipe);

      /* We unlock SOCK here, as it's not subsequently used, and we might
	 go to sleep waiting for readable data.  */
      mutex_unlock (&sock->lock);

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
    mutex_unlock (&sock->lock);

  return 0;
}

/* Return the current status of the object.  Not all the fields of the
   io_statuf_t are meaningful for all objects; however, the access and
   modify times, the optimal IO size, and the fs type are meaningful
   for all objects.  */
error_t
S_io_stat (struct sock_user *user, struct stat *st)
{
  struct sock *sock;
  void copy_time (time_value_t from, time_t *to_sec, unsigned long *to_usec)
    {
      *to_sec = from.seconds;
      *to_usec = from.microseconds;
    }

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;

  bzero (st, sizeof (struct stat));

  st->st_fstype = FSTYPE_SOCKET;
  st->st_fsid = getpid ();
  st->st_ino = sock->id;

  st->st_blksize = vm_page_size * 8;

  mutex_lock (&sock->lock);	/* Make sure the pipes don't go away...  */

  if (sock->read_pipe)
    copy_time (&sock->read_pipe->read_time, &st->st_atime, &st->atime_usec);
  if (sock->write_pipe)
    copy_time (&sock->read_pipe->write_time, &st->st_mtime, &st->mtime_usec);
  copy_time (&sock->change_time, &st->st_ctime, &st->ctime_usec);

  return 0;
}
