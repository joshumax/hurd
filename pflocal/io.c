/* Socket I/O operations

   Copyright (C) 1995, 1996, 1998, 1999, 2000, 2002, 2007, 2012
     Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
#include <unistd.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <hurd.h>		/* for getauth() */
#include <hurd/hurd_types.h>
#include <hurd/auth.h>
#include <hurd/pipe.h>
#include <mach/notify.h>

#include "sock.h"
#include "connq.h"
#include "sserver.h"

#include "io_S.h"

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in amount.  */
error_t
S_io_read (struct sock_user *user,
	   data_t *data, mach_msg_type_number_t *data_len,
	   off_t offset, mach_msg_type_number_t amount)
{
  error_t err;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  err = sock_acquire_read_pipe (user->sock, &pipe);
  if (err == EPIPE)
    /* EOF */
    {
      err = 0;
      *data_len = 0;
    }
  else if (!err)
    {
      err =
	pipe_read (pipe, user->sock->flags & PFLOCAL_SOCK_NONBLOCK, NULL,
		   data, data_len, amount);
      pipe_release_reader (pipe);
    }

  return err;
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
	    data_t data, mach_msg_type_number_t data_len,
	    off_t offset, mach_msg_type_number_t *amount)
{
  error_t err;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  err = sock_acquire_write_pipe (user->sock, &pipe);
  if (!err)
    {
      struct addr *source_addr;

      /* We could provide a source address for all writes, but we only do so
	 for connectionless sockets because that's the only place it's
	 required, and it's more efficient not to.  */
      if (pipe->class->flags & PIPE_CLASS_CONNECTIONLESS)
	err = sock_get_addr (user->sock, &source_addr);
      else
	source_addr = NULL;

      if (!err)
	{
	  err = pipe_write (pipe, user->sock->flags & PFLOCAL_SOCK_NONBLOCK,
			    source_addr, data, data_len, amount);
	  if (err && source_addr)
	    ports_port_deref (source_addr);
	}

      pipe_release_writer (pipe);
    }

  return err;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
error_t
S_io_readable (struct sock_user *user, mach_msg_type_number_t *amount)
{
  error_t err;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  err = sock_acquire_read_pipe (user->sock, &pipe);
  if (err == EPIPE)
    /* EOF */
    {
      err = 0;
      *amount = 0;
    }
  else if (!err)
    {
      *amount = pipe_readable (user->sock->read_pipe, 1);
      pipe_release_reader (pipe);
    }

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
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  err = sock_create_port (user->sock, new_port);
  if (! err)
    *new_port_type = MACH_MSG_TYPE_MAKE_SEND;

  return err;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  */
static error_t
io_select_common (struct sock_user *user,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  struct timespec *tsp, int *select_type)
{
  error_t err = 0;
  struct sock *sock;

  if (!user)
    return EOPNOTSUPP;

  *select_type &= SELECT_READ | SELECT_WRITE;

  sock = user->sock;
  pthread_mutex_lock (&sock->lock);

  if (sock->listen_queue)
    /* Sock is used for accepting connections, not I/O.  For these, you can
       only select for reading, which will block until a connection request
       comes along.  */
    {
      pthread_mutex_unlock (&sock->lock);

      *select_type &= SELECT_READ;

      if (*select_type & SELECT_READ)
	{
	  struct timespec noblock = {0, 0};

	  /* Wait for a connect.  Passing in NULL for SOCK means that
	     the request won't be dequeued.  */
	  if (connq_listen (sock->listen_queue, &noblock, NULL) == 0)
	    /* We can satisfy this request immediately. */
	    return 0;
	  else
	    /* Gotta wait...  */
	    {
	      ports_interrupt_self_on_port_death (user, reply);
	      err = connq_listen (sock->listen_queue, tsp, NULL);
	      if (err == ETIMEDOUT)
		{
		  *select_type = 0;
		  err = 0;
		}
	      return err;
	    }
	}
    }
  else
    /* Sock is a normal read/write socket.  */
    {
      int valid;
      int ready = 0;
      struct pipe *read_pipe = sock->read_pipe;
      struct pipe *write_pipe = sock->write_pipe;

      if (! write_pipe)
	ready |= SELECT_WRITE;
      if (! read_pipe)
	ready |= SELECT_READ;
      ready &= *select_type; /* Only keep things requested.  */
      *select_type &= ~ready;

      valid = *select_type;
      if (valid & SELECT_READ)
	{
	  pipe_acquire_reader (read_pipe);
	  err = pipe_wait_readable (read_pipe, 1, 1);
	  if (err == EWOULDBLOCK)
	    err = 0; /* Not readable, actually not an error.  */
	  else
	    ready |= SELECT_READ; /* Data immediately readable (or error).  */
	  pthread_mutex_unlock (&read_pipe->lock);
	  if (err)
	    /* Prevent write test from overwriting err.  */
	    valid &= ~SELECT_WRITE;
	}
      if (valid & SELECT_WRITE)
	{
	  pipe_acquire_writer (write_pipe);
	  err = pipe_wait_writable (write_pipe, 1);
	  if (err == EWOULDBLOCK)
	    err = 0; /* Not writable, actually not an error.  */
	  else
	    ready |= SELECT_WRITE; /* Data immediately writable (or error).  */
	  pthread_mutex_unlock (&write_pipe->lock);
	}

      pthread_mutex_unlock (&sock->lock);

      if (ready)
	/* No need to block, we've already got some results.  */
	*select_type = ready;
      else
	/* Wait for something to change.  */
	{
	  ports_interrupt_self_on_port_death (user, reply);
	  err = pipe_pair_select (read_pipe, write_pipe, tsp, select_type, 1);
	}

      if (valid & SELECT_READ)
	pipe_remove_reader (read_pipe);
      if (valid & SELECT_WRITE)
	pipe_remove_writer (write_pipe);
    }

  return err;
}

error_t
S_io_select (struct sock_user *user,
	     mach_port_t reply, mach_msg_type_name_t reply_type,
	     int *select_type)
{
  return io_select_common (user, reply, reply_type, NULL, select_type);
}

error_t
S_io_select_timeout (struct sock_user *user,
		     mach_port_t reply, mach_msg_type_name_t reply_type,
		     struct timespec ts,
		     int *select_type)
{
  return io_select_common (user, reply, reply_type, &ts, select_type);
}

static inline void
copy_time (time_value_t *from, time_t *to_sec, long *to_nsec)
{
  *to_sec = from->seconds;
  *to_nsec = from->microseconds * 1000;
}

/* Return the current status of the object.  Not all the fields of the
   io_statuf_t are meaningful for all objects; however, the access and
   modify times, the optimal IO size, and the fs type are meaningful
   for all objects.  */
error_t
S_io_stat (struct sock_user *user, struct stat *st)
{
  struct sock *sock;
  struct pipe *rpipe, *wpipe;

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;

  memset (st, 0, sizeof (struct stat));

  st->st_fstype = FSTYPE_SOCKET;
  st->st_mode = sock->mode;
  st->st_fsid = getpid ();
  st->st_ino = sock->id;
  /* As we try to be clever with large transfers, ask for them. */
  st->st_blksize = vm_page_size * 16;

  pthread_mutex_lock (&sock->lock);	/* Make sure the pipes don't go away...  */

  rpipe = sock->read_pipe;
  wpipe = sock->write_pipe;

  if (rpipe)
    {
      pthread_mutex_lock (&rpipe->lock);
      copy_time (&rpipe->read_time, &st->st_atim.tv_sec, &st->st_atim.tv_nsec);
      /* This seems useful.  */
      st->st_size = pipe_readable (rpipe, 1);
      pthread_mutex_unlock (&rpipe->lock);
    }

  if (wpipe)
    {
      pthread_mutex_lock (&wpipe->lock);
      copy_time (&wpipe->write_time, &st->st_mtim.tv_sec, &st->st_mtim.tv_nsec);
      pthread_mutex_unlock (&wpipe->lock);
    }

  copy_time (&sock->change_time, &st->st_ctim.tv_sec, &st->st_ctim.tv_nsec);

  pthread_mutex_unlock (&sock->lock);

  return 0;
}

error_t
S_io_get_openmodes (struct sock_user *user, int *bits)
{
  unsigned flags;
  if (!user)
    return EOPNOTSUPP;
  flags = user->sock->flags;
  *bits =
      O_APPEND			/* pipes always append */
    | (flags & PFLOCAL_SOCK_NONBLOCK ? O_NONBLOCK : 0)
    | (flags & PFLOCAL_SOCK_SHUTDOWN_READ ? 0 : O_READ)
    | (flags & PFLOCAL_SOCK_SHUTDOWN_WRITE ? 0 : O_WRITE);
  return 0;
}

error_t
S_io_set_all_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->sock->lock);
  if (bits & O_NONBLOCK)
    user->sock->flags |= PFLOCAL_SOCK_NONBLOCK;
  else
    user->sock->flags &= ~PFLOCAL_SOCK_NONBLOCK;
  pthread_mutex_unlock (&user->sock->lock);

  return 0;
}

error_t
S_io_set_some_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->sock->lock);
  if (bits & O_NONBLOCK)
    user->sock->flags |= PFLOCAL_SOCK_NONBLOCK;
  pthread_mutex_unlock (&user->sock->lock);

  return 0;
}

error_t
S_io_clear_some_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->sock->lock);
  if (bits & O_NONBLOCK)
    user->sock->flags &= ~PFLOCAL_SOCK_NONBLOCK;
  pthread_mutex_unlock (&user->sock->lock);

  return 0;
}

#define NIDS 10

error_t
S_io_reauthenticate (struct sock_user *user, mach_port_t rendezvous)
{
  error_t err;
  mach_port_t auth_server;
  mach_port_t new_user_port;
  uid_t uids_buf[NIDS], aux_uids_buf[NIDS];
  uid_t *uids = uids_buf, *aux_uids = aux_uids_buf;
  gid_t gids_buf[NIDS], aux_gids_buf[NIDS];
  gid_t *gids = gids_buf, *aux_gids = aux_gids_buf;
  size_t num_uids = NIDS, num_aux_uids = NIDS;
  size_t num_gids = NIDS, num_aux_gids = NIDS;

  if (!user)
    return EOPNOTSUPP;

  do
    err = sock_create_port (user->sock, &new_user_port);
  while (err == EINTR);
  if (err)
    return err;

  auth_server = getauth ();
  err = mach_port_insert_right (mach_task_self (), new_user_port,
				new_user_port, MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);
  do
    err =
      auth_server_authenticate (auth_server,
				rendezvous, MACH_MSG_TYPE_COPY_SEND,
				new_user_port, MACH_MSG_TYPE_COPY_SEND,
				&uids, &num_uids, &aux_uids, &num_aux_uids,
				&gids, &num_gids, &aux_gids, &num_aux_gids);
  while (err == EINTR);
  mach_port_deallocate (mach_task_self (), rendezvous);
  mach_port_deallocate (mach_task_self (), auth_server);
  mach_port_deallocate (mach_task_self (), new_user_port);

  /* Throw away the ids we went through all that trouble to get... */
#define TRASH_IDS(ids, buf, num) \
  if (buf != ids) \
    munmap (ids, num * sizeof (uid_t));

  TRASH_IDS (uids, uids_buf, num_uids);
  TRASH_IDS (gids, gids_buf, num_gids);
  TRASH_IDS (aux_uids, aux_uids_buf, num_aux_uids);
  TRASH_IDS (aux_gids, aux_gids_buf, num_aux_gids);

  return err;
}

error_t
S_io_restrict_auth (struct sock_user *user,
		    mach_port_t *new_port,
		    mach_msg_type_name_t *new_port_type,
		    uid_t *uids, size_t num_uids,
		    uid_t *gids, size_t num_gids)
{
  if (!user)
    return EOPNOTSUPP;
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return sock_create_port (user->sock, new_port);
}

error_t
S_io_pathconf (struct sock_user *user, int name, int *value)
{
  if (user == NULL)
    return EOPNOTSUPP;
  else if (name == _PC_PIPE_BUF)
    {
      pthread_mutex_lock (&user->sock->lock);
      if (user->sock->write_pipe == NULL)
	*value = 0;
      else
	*value = user->sock->write_pipe->write_atomic;
      pthread_mutex_unlock (&user->sock->lock);
      return 0;
    }
  else
    return EINVAL;
}

error_t
S_io_identity (struct sock_user *user,
	       mach_port_t *id, mach_msg_type_name_t *id_type,
	       mach_port_t *fsys_id, mach_msg_type_name_t *fsys_id_type,
	       ino_t *fileno)
{
  static mach_port_t server_id = MACH_PORT_NULL;
  error_t err = 0;
  struct sock *sock;

  if (! user)
    return EOPNOTSUPP;

  if (server_id == MACH_PORT_NULL)
    {
      static pthread_mutex_t server_id_lock = PTHREAD_MUTEX_INITIALIZER;

      pthread_mutex_lock (&server_id_lock);
      if (server_id == MACH_PORT_NULL) /* Recheck with the lock held.  */
	err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				  &server_id);
      pthread_mutex_unlock (&server_id_lock);

      if (err)
	return err;
    }

  sock = user->sock;

  pthread_mutex_lock (&sock->lock);
  if (sock->id == MACH_PORT_NULL)
    err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			      &sock->id);
  pthread_mutex_unlock (&sock->lock);

  if (! err)
    {
      *id = sock->id;
      *id_type = MACH_MSG_TYPE_MAKE_SEND;
      *fsys_id = server_id;
      *fsys_id_type = MACH_MSG_TYPE_MAKE_SEND;
      *fileno = sock->id;	/* Might as well */
    }

  return err;
}
