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

#include <string.h>		/* For bzero() */
#include <unistd.h>		/* For getpid() */
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>

#include <hurd.h>		/* for getauth() */
#include <hurd/hurd_types.h>
#include <hurd/auth.h>

#include "sock.h"
#include "pipe.h"
#include "connq.h"
#include "sserver.h"

#include "io_S.h"
#include "interrupt_S.h"

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in amount.  */
error_t
S_io_read (struct sock_user *user,
	   char **data, mach_msg_type_number_t *data_len,
	   off_t offset, mach_msg_type_number_t amount)
{
  error_t err;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  err = sock_aquire_read_pipe (user->sock, &pipe);
  if (!err)
    {
      err =
	pipe_read (pipe, user->sock->flags & SOCK_NONBLOCK, NULL, NULL,
		   data, data_len, amount, NULL, NULL, NULL, NULL);
      pipe_release (pipe);
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
	    char *data, mach_msg_type_number_t data_len,
	    off_t offset, mach_msg_type_number_t *amount)
{
  error_t err;
  struct pipe *pipe;

  if (!user)
    return EOPNOTSUPP;

  err = sock_aquire_write_pipe (user->sock, &pipe);
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
	  err = pipe_write (pipe, source_addr,
			    data, data_len, NULL, 0, NULL, 0,
			    amount);
	  if (source_addr)
	    ports_port_deref (source_addr);
	}

      pipe_release (pipe);
    }

  return err;
}

/* Cause a pending request on this object to immediately return.  The
   exact semantics are dependent on the specific object.  */
error_t
S_interrupt_operation (mach_port_t port)
{
  struct pipe *pipe;
  struct sock_user *user = ports_lookup_port (sock_port_bucket, port, 0);

  if (!user)
    return EOPNOTSUPP;
debug (user, "interrupt, sock: %p", user->sock);

  /* Interrupt pending reads on this socket.  We don't bother with writes
     since they never block.  */
  if (sock_aquire_read_pipe (user->sock, &pipe) == 0)
    {
      /* Indicate to currently waiting threads they've been interrupted.  */
      pipe->interrupt_seq_num++;
      pipe_kick (pipe);
      pipe_release (pipe);
    }

  ports_port_deref (user);

  return 0;
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

  err = sock_aquire_read_pipe (user->sock, &pipe);
  if (!err)
    {
      *amount = pipe_readable (user->sock->read_pipe, 1);
      pipe_release (pipe);
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
  if (!user)
    return EOPNOTSUPP;
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return sock_create_port (user->sock, new_port);
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
error_t
S_io_select (struct sock_user *user, int *select_type, int *id_tag)
{
  error_t err = 0;
  struct sock *sock;

  if (!user)
    return EOPNOTSUPP;

  *select_type |= ~SELECT_URG;	/* We never return these.  */

  sock = user->sock;
debug (sock, "lock");
  mutex_lock (&sock->lock);

  if (sock->connq)
    /* Sock is used for accepting connections, not I/O.  For these, you can
       only select for reading, which will block until a connection request
       comes along.  */
    {
debug (sock, "unlock");
      mutex_unlock (&sock->lock);

      if (*select_type & SELECT_WRITE)
	/* Meaningless for a non-i/o socket.  */
debug (sock, "ebadf");
	return EBADF;

      if (*select_type & SELECT_READ)
	/* Wait for a connect.  Passing in NULL for REQ means that the
	   request won't be dequeued.  */
{debug (sock, "waiting for connection");
	return 
	  connq_listen (sock->connq, sock->flags & SOCK_NONBLOCK, NULL, NULL);
}
    }
  else
    /* Sock is a normal read/write socket.  */
    {
      if ((*select_type & SELECT_WRITE) && !sock->write_pipe)
	{
debug (sock, "unlock");
	  mutex_unlock (&sock->lock);
debug (sock, "ebadf");
	  return EBADF;
	}
      /* Otherwise, pipes are always writable... */

      if (*select_type & SELECT_READ)
	{
	  struct pipe *pipe = sock->read_pipe;

	  pipe_aquire (pipe);

	  /* We unlock SOCK here, as it's not subsequently used, and we might
	     go to sleep waiting for readable data.  */
debug (sock, "unlock");
	  mutex_unlock (&sock->lock);

	  if (!pipe)
{debug (sock, "ebadf");
	    return EBADF;
}

	  if (! pipe_is_readable (pipe, 1))
	    /* Nothing to read on PIPE yet...  */
	    if (*select_type & ~SELECT_READ)
	      /* But there's other stuff to report, so return that.  */
	      *select_type &= ~SELECT_READ;
	    else
	      /* The user only cares about reading, so wait until something is
		 readable.  */
	      err = pipe_wait (pipe, 0, 1);

	  pipe_release (pipe);
	}
      else
{debug (sock, "unlock");
	mutex_unlock (&sock->lock);
}
    }

debug (sock, "out");
  return err;
}

/* Return the current status of the object.  Not all the fields of the
   io_statuf_t are meaningful for all objects; however, the access and
   modify times, the optimal IO size, and the fs type are meaningful
   for all objects.  */
error_t
S_io_stat (struct sock_user *user, struct stat *st)
{
  struct sock *sock;
  void copy_time (time_value_t *from, time_t *to_sec, unsigned long *to_usec)
    {
      *to_sec = from->seconds;
      *to_usec = from->microseconds;
    }

  if (!user)
    return EOPNOTSUPP;

  sock = user->sock;

  bzero (st, sizeof (struct stat));

  st->st_fstype = FSTYPE_SOCKET;
  st->st_fsid = getpid ();
  st->st_ino = sock->id;

  st->st_blksize = vm_page_size * 8;

debug (sock, "lock");
  mutex_lock (&sock->lock);	/* Make sure the pipes don't go away...  */

  if (sock->read_pipe)
    copy_time (&sock->read_pipe->read_time, &st->st_atime, &st->st_atime_usec);
  if (sock->write_pipe)
    copy_time (&sock->read_pipe->write_time, &st->st_mtime, &st->st_mtime_usec);
  copy_time (&sock->change_time, &st->st_ctime, &st->st_ctime_usec);

debug (sock, "unlock");
  mutex_unlock (&sock->lock);

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
      (flags & SOCK_NONBLOCK ? O_NONBLOCK : 0)
    | (flags & SOCK_SHUTDOWN_READ ? 0 : O_READ)
    | (flags & SOCK_SHUTDOWN_WRITE ? 0 : O_WRITE);
  return 0;
}

error_t
S_io_set_all_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;
debug (user->sock, "lock");
  mutex_lock (&user->sock->lock);
  if (bits & SOCK_NONBLOCK)
    user->sock->flags |= SOCK_NONBLOCK;
  else
    user->sock->flags &= ~SOCK_NONBLOCK;
debug (user->sock, "unlock");
  mutex_unlock (&user->sock->lock);
  return 0;
}

error_t
S_io_set_some_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;
debug (user->sock, "lock");
  mutex_lock (&user->sock->lock);
  if (bits & SOCK_NONBLOCK)
    user->sock->flags |= SOCK_NONBLOCK;
debug (user->sock, "unlock");
  mutex_unlock (&user->sock->lock);
  return 0;
}

error_t
S_io_clear_some_openmodes (struct sock_user *user, int bits)
{
  if (!user)
    return EOPNOTSUPP;
debug (user->sock, "lock");
  mutex_lock (&user->sock->lock);
  if (bits & SOCK_NONBLOCK)
    user->sock->flags &= ~SOCK_NONBLOCK;
debug (user->sock, "unlock");
  mutex_unlock (&user->sock->lock);
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
  unsigned num_uids = NIDS, num_aux_uids = NIDS;
  unsigned num_gids = NIDS, num_aux_gids = NIDS;

  if (!user)
    return EOPNOTSUPP;
  
  err = sock_create_port (user->sock, &new_user_port);
  if (err)
    return err;

debug (user, "old: %p, new: %d", user, new_user_port);
  auth_server = getauth ();
  err =
    auth_server_authenticate (auth_server, ports_get_right (user), 
			      MACH_MSG_TYPE_MAKE_SEND, 
			      rendezvous, MACH_MSG_TYPE_MOVE_SEND,
			      new_user_port, MACH_MSG_TYPE_MAKE_SEND, 
			      &uids, &num_uids, &aux_uids, &num_aux_uids,
			      &gids, &num_gids, &aux_gids, &num_aux_gids);
  mach_port_deallocate (mach_task_self (), auth_server);

  /* Throw away the ids we went through all that trouble to get... */
#define TRASH_IDS(ids, buf, num) \
  if (buf != ids) \
    vm_deallocate (mach_task_self (), (vm_address_t)ids, num * sizeof (uid_t));

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
		    uid_t *uids, unsigned num_uids,
		    uid_t *gids, unsigned num_gids)
{
  if (!user)
    return EOPNOTSUPP;
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return sock_create_port (user->sock, new_port);
}

/* Stubs for currently unsupported rpcs.  */

error_t
S_io_async(struct sock_user *user,
	   mach_port_t notify_port,
	   mach_port_t *async_id_port,
	   mach_msg_type_name_t *async_id_port_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_mod_owner(struct sock_user *user, pid_t owner)
{
  return EOPNOTSUPP;
}

error_t 
S_io_get_owner(struct sock_user *user, pid_t *owner)
{
  return EOPNOTSUPP;
}

error_t
S_io_get_icky_async_id (struct sock_user *user,
			mach_port_t *icky_async_id_port,
			mach_msg_type_name_t *icky_async_id_port_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_map (struct sock_user *user,
	  mach_port_t *memobj_rd, mach_msg_type_name_t *memobj_rd_type,
	  mach_port_t *memobj_wt, mach_msg_type_name_t *memobj_wt_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_map_cntl (struct sock_user *user,
	       mach_port_t *mem, mach_msg_type_name_t *mem_type)
{
  return EOPNOTSUPP;
}

error_t
S_io_get_conch (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_release_conch (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_eofnotify (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_prenotify (struct sock_user *user, vm_offset_t start, vm_offset_t end)
{
  return EOPNOTSUPP;
}

error_t
S_io_postnotify (struct sock_user *user, vm_offset_t start, vm_offset_t end)
{
  return EOPNOTSUPP;
}

error_t
S_io_readsleep (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_readnotify (struct sock_user *user)
{
  return EOPNOTSUPP;
}


error_t
S_io_sigio (struct sock_user *user)
{
  return EOPNOTSUPP;
}

error_t
S_io_server_version (struct sock_user *user,
		     char *name, int *maj, int *min, int *edit)
{
  return EOPNOTSUPP;
}
