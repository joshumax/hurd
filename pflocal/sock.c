/* Sock functions

   Copyright (C) 1995,96,2000,01,02, 2005 Free Software Foundation, Inc.
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

#include <pthread.h>

#include <hurd/pipe.h>

#include "sock.h"
#include "sserver.h"
#include "connq.h"

/* ---------------------------------------------------------------- */

/* Returns the pipe that SOCK is reading from in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  In the
   case where the read should signal EOF, EPIPE is returned.  SOCK mustn't be
   locked.  */
error_t
sock_acquire_read_pipe (struct sock *sock, struct pipe **pipe)
{
  error_t err = 0;

  pthread_mutex_lock (&sock->lock);

  *pipe = sock->read_pipe;
  if (*pipe != NULL)
    /* SOCK may have a read pipe even before it's connected, so make
       sure it really is.  */
    if (   !(sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
	&& !(sock->flags & PFLOCAL_SOCK_CONNECTED))
      err = ENOTCONN;
    else
      pipe_acquire_reader (*pipe);
  else if (sock->flags & PFLOCAL_SOCK_SHUTDOWN_READ)
    /* Reading on a socket with the read-half shutdown always acts as if the
       pipe were at eof, even if the socket isn't connected yet [at least in
       netbsd].  */
    err = EPIPE;
  else
    err = ENOTCONN;

  pthread_mutex_unlock (&sock->lock);

  return err;
}

/* Returns the pipe that SOCK is writing to in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t
sock_acquire_write_pipe (struct sock *sock, struct pipe **pipe)
{
  error_t err = 0;

  pthread_mutex_lock (&sock->lock);
  *pipe = sock->write_pipe;
  if (*pipe != NULL)
    pipe_acquire_writer (*pipe); /* Do this before unlocking the sock!  */
  else if (sock->flags & PFLOCAL_SOCK_SHUTDOWN_WRITE)
    /* Writing on a socket with the write-half shutdown always acts as if the
       pipe were broken, even if the socket isn't connected yet [at least in
       netbsd].  */
    err = EPIPE;
  else if (sock->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS)
    /* Connectionless protocols give a different error when unconnected.  */
    err = EDESTADDRREQ;
  else
    err = ENOTCONN;

  pthread_mutex_unlock (&sock->lock);

  return err;
}

/* ---------------------------------------------------------------- */

/* Return a new socket with the given pipe class in SOCK.  */
error_t
sock_create (struct pipe_class *pipe_class, mode_t mode, struct sock **sock)
{
  error_t err;
  struct sock *new = malloc (sizeof (struct sock));

  if (new == NULL)
    return ENOMEM;

  /* A socket always has a read pipe (this is just to avoid some annoyance in
     sock_connect), so create it here.  */
  err = pipe_create (pipe_class, &new->read_pipe);
  if (err)
    {
      free (new);
      return err;
    }

  pipe_add_reader (new->read_pipe);

  new->refs = 0;
  new->flags = 0;
  new->write_pipe = NULL;
  new->mode = mode;
  new->id = MACH_PORT_NULL;
  new->listen_queue = NULL;
  new->connect_queue = NULL;
  new->pipe_class = pipe_class;
  new->addr = NULL;
  memset (&new->change_time, 0, sizeof (new->change_time));
  pthread_mutex_init (&new->lock, NULL);

  *sock = new;
  return 0;
}

/* Free SOCK, assuming there are no more handle on it.  */
void
sock_free (struct sock *sock)
{
  sock_shutdown (sock, PFLOCAL_SOCK_SHUTDOWN_READ | PFLOCAL_SOCK_SHUTDOWN_WRITE);
  if (sock->id != MACH_PORT_NULL)
    mach_port_destroy (mach_task_self (), sock->id);
  if (sock->listen_queue)
    connq_destroy (sock->listen_queue);
  free (sock);
}

/* Free a sock derefed too far.  */
void
_sock_norefs (struct sock *sock)
{
  /* A sock should never have an address when it has 0 refs, as the
     address should hold a reference to the sock!  */
  assert_backtrace (sock->addr == NULL);
  pthread_mutex_unlock (&sock->lock);	/* Unlock so sock_free can do stuff.  */
  sock_free (sock);
}

/* ---------------------------------------------------------------- */

/* Return a new socket largely copied from TEMPLATE.  */
error_t
sock_clone (struct sock *template, struct sock **sock)
{
  error_t err = sock_create (template->pipe_class, template->mode, sock);

  if (err)
    return err;

  /* Copy some properties from TEMPLATE.  */
  (*sock)->flags = template->flags & ~PFLOCAL_SOCK_CONNECTED;

  return 0;
}

/* ---------------------------------------------------------------- */

struct port_class *sock_user_port_class;

/* Get rid of a user reference to a socket.  */
static void
sock_user_clean (void *vuser)
{
  struct sock_user *user = vuser;
  sock_deref (user->sock);
}

/* Return a new user port on SOCK in PORT.  */
error_t
sock_create_port (struct sock *sock, mach_port_t *port)
{
  struct sock_user *user;
  error_t err =
    ports_create_port (sock_user_port_class, sock_port_bucket,
		       sizeof (struct sock_user), &user);

  if (err)
    return err;

  ensure_sock_server ();

  pthread_mutex_lock (&sock->lock);
  sock->refs++;
  pthread_mutex_unlock (&sock->lock);

  user->sock = sock;

  *port = ports_get_right (user);
  ports_port_deref (user);	/* We only want one ref, for the send right. */

  return 0;
}

/* ---------------------------------------------------------------- */
/* Address manipulation.  */

struct addr
{
  struct port_info pi;
  struct sock *sock;
  pthread_mutex_t lock;
};

struct port_class *addr_port_class;

/* Get rid of ADDR's socket's reference to it, in preparation for ADDR going
   away.  */
static void
addr_unbind (void *vaddr)
{
  struct sock *sock;
  struct addr *addr = vaddr;

  pthread_mutex_lock (&addr->lock);
  sock = addr->sock;
  if (sock)
    {
      pthread_mutex_lock (&sock->lock);
      sock->addr = NULL;
      addr->sock = NULL;
      ports_port_deref_weak (addr);
      pthread_mutex_unlock (&sock->lock);
      sock_deref (sock);
    }
  pthread_mutex_unlock (&addr->lock);
}

/* Cleanup after the address ADDR, which is going away... */
static void
addr_clean (void *vaddr)
{
  struct addr *addr = vaddr;
  /* ADDR should never have a socket bound to it at this point, as it should
     have been removed by addr_unbind dropping the socket's weak reference
     it.  */
  assert_backtrace (addr->sock == NULL);
}

/* Return a new address, not connected to any socket yet, ADDR.  */
inline error_t
addr_create (struct addr **addr)
{
  error_t err =
    ports_create_port (addr_port_class, sock_port_bucket,
		       sizeof (struct addr), addr);

  if (! err)
    {
      ensure_sock_server ();
      (*addr)->sock = NULL;
      pthread_mutex_init (&(*addr)->lock, NULL);
    }

  return err;
}

/* Bind SOCK to ADDR.  */
error_t
sock_bind (struct sock *sock, struct addr *addr)
{
  error_t err = 0;
  struct addr *old_addr;

  if (addr)
    pthread_mutex_lock (&addr->lock);
  pthread_mutex_lock (&sock->lock);

  old_addr = sock->addr;
  if (addr && old_addr)
    err = EINVAL;		/* SOCK already bound.  */
  else if (!addr && !old_addr)
    err = EINVAL;		/* SOCK already bound.  */
  else if (addr && addr->sock)
    err = EADDRINUSE;		/* Something else already bound ADDR.  */
  else if (addr)
    addr->sock = sock;		/* First binding for SOCK.  */
  else
    old_addr->sock = NULL;	/* Unbinding SOCK.  */

  if (! err)
    {
      sock->addr = addr;
      if (addr)
	{
	  sock->refs++;
	  ports_port_ref_weak (addr);
	}
      if (old_addr)
	{
	  /* Note that we don't have to worry about SOCK's ref count going to
	     zero because whoever's calling us should be holding a ref.  */
	  sock->refs--;
	  ports_port_deref_weak (old_addr);
	  assert_backtrace (sock->refs > 0);	/* But make sure... */
	}
    }

  pthread_mutex_unlock (&sock->lock);
  if (addr)
    pthread_mutex_unlock (&addr->lock);

  return err;
}

/* Returns SOCK's addr, with an additional reference, fabricating one if
   necessary.  SOCK should be locked.  */
static inline error_t
ensure_addr (struct sock *sock, struct addr **addr)
{
  error_t err = 0;

  if (! sock->addr)
    {
      err = addr_create (&sock->addr);
      if (!err)
	{
	  sock->addr->sock = sock;
	  sock->refs++;
	  ports_port_ref_weak (sock->addr);
	}
    }
  else
    ports_port_ref (sock->addr);

  if (!err)
    *addr = sock->addr;

  return err;
}

/* Returns the socket bound to ADDR in SOCK, or EADDRNOTAVAIL.  The returned
   sock will have one reference added to it.  */
error_t
addr_get_sock (struct addr *addr, struct sock **sock)
{
  pthread_mutex_lock (&addr->lock);
  *sock = addr->sock;
  if (*sock)
    (*sock)->refs++;
  pthread_mutex_unlock (&addr->lock);
  return *sock ? 0 : EADDRNOTAVAIL;
}

/* Returns SOCK's address in ADDR, with an additional reference added.  If
   SOCK doesn't currently have an address, one is fabricated first.  */
error_t
sock_get_addr (struct sock *sock, struct addr **addr)
{
  error_t err;

  pthread_mutex_lock (&sock->lock);
  err = ensure_addr (sock, addr);
  pthread_mutex_unlock (&sock->lock);

  return err;			/* XXX */
}

/* ---------------------------------------------------------------- */

/* We hold this lock before we lock two sockets at once, to prevent someone
   else trying to lock the same two sockets in the reverse order, resulting
   in a deadlock.  */
static pthread_mutex_t socket_pair_lock;

/* Connect SOCK1 and SOCK2.  */
error_t
sock_connect (struct sock *sock1, struct sock *sock2)
{
  error_t err = 0;
  /* In the case of a connectionless protocol, an already-connected socket may
     be reconnected, so save the old destination for later disposal.  */
  struct pipe *old_sock1_write_pipe = NULL;
  struct addr *old_sock1_write_addr = NULL;

  void connect (struct sock *wr, struct sock *rd)
    {
      if (!(   (wr->flags & PFLOCAL_SOCK_SHUTDOWN_WRITE)
	    || (rd->flags & PFLOCAL_SOCK_SHUTDOWN_READ)))
	{
	  struct pipe *pipe = rd->read_pipe;
	  assert_backtrace (pipe);	/* Since PFLOCAL_SOCK_SHUTDOWN_READ isn't set.  */
	  pipe_add_writer (pipe);
	  wr->write_pipe = pipe;
	}
    }

  if (sock1->pipe_class != sock2->pipe_class)
    /* Incompatible socket types.  */
    return EOPNOTSUPP;		/* XXX?? */

  pthread_mutex_lock (&socket_pair_lock);
  pthread_mutex_lock (&sock1->lock);
  if (sock1 != sock2)
    /* If SOCK1 == SOCK2, then we get a fifo!  */
    pthread_mutex_lock (&sock2->lock);

  if ((sock1->flags & PFLOCAL_SOCK_CONNECTED) || (sock2->flags & PFLOCAL_SOCK_CONNECTED))
    /* An already-connected socket.  */
    err = EISCONN;
  else
    {
      old_sock1_write_pipe = sock1->write_pipe;
      old_sock1_write_addr = sock1->write_addr;

      /* Always make the forward connection.  */
      connect (sock1, sock2);

      /* Only make the reverse for connection-oriented protocols.  */
      if (! (sock1->pipe_class->flags & PIPE_CLASS_CONNECTIONLESS))
	{
	  sock1->flags |= PFLOCAL_SOCK_CONNECTED;
	  if (sock1 != sock2)
	    {
	      connect (sock2, sock1);
	      sock2->flags |= PFLOCAL_SOCK_CONNECTED;
	    }
	}
    }

  if (sock1 != sock2)
    pthread_mutex_unlock (&sock2->lock);
  pthread_mutex_unlock (&sock1->lock);
  pthread_mutex_unlock (&socket_pair_lock);

  if (old_sock1_write_pipe)
    {
      pipe_remove_writer (old_sock1_write_pipe);
      ports_port_deref (old_sock1_write_addr);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Shutdown either the read or write halves of SOCK, depending on whether the
   PFLOCAL_SOCK_SHUTDOWN_READ or PFLOCAL_SOCK_SHUTDOWN_WRITE flags are set in FLAGS.  */
void
sock_shutdown (struct sock *sock, unsigned flags)
{
  unsigned old_flags;
  struct pipe *read_pipe = NULL;
  struct pipe *write_pipe = NULL;

  pthread_mutex_lock (&sock->lock);

  old_flags = sock->flags;
  sock->flags |= flags;

  if (flags & PFLOCAL_SOCK_SHUTDOWN_READ && !(old_flags & PFLOCAL_SOCK_SHUTDOWN_READ))
    {
      /* Shutdown the read half.  */
      read_pipe = sock->read_pipe;
      sock->read_pipe = NULL;
    }
  if (flags & PFLOCAL_SOCK_SHUTDOWN_WRITE && !(old_flags & PFLOCAL_SOCK_SHUTDOWN_WRITE))
    {
      /* Shutdown the write half.  */
      write_pipe = sock->write_pipe;
      sock->write_pipe = NULL;
    }

  /* Unlock SOCK here, as we may subsequently wake up other threads. */
  pthread_mutex_unlock (&sock->lock);
  
  if (read_pipe)
    pipe_remove_reader (read_pipe);
  if (write_pipe)
    pipe_remove_writer (write_pipe);
}

/* ---------------------------------------------------------------- */

error_t
sock_global_init ()
{
  sock_port_bucket = ports_create_bucket ();
  sock_user_port_class = ports_create_class (sock_user_clean, NULL);
  addr_port_class = ports_create_class (addr_clean, addr_unbind);
  return 0;
}

/* Try to shutdown any active sockets, returning EBUSY if we can't.  */
error_t
sock_global_shutdown ()
{
  int num_ports = ports_count_bucket (sock_port_bucket);
  ports_enable_bucket (sock_port_bucket);
  return (num_ports == 0 ? 0 : EBUSY);
}
