/* Internal sockets

   Copyright (C) 1995,96,99,2000,01 Free Software Foundation, Inc.

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

#ifndef __SOCK_H__
#define __SOCK_H__

#include <assert-backtrace.h>
#include <pthread.h>		/* For mutexes */
#include <sys/mman.h>
#include <sys/types.h>

#include <hurd/ports.h>

struct pipe;
struct pipe_class;

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
  pthread_mutex_t lock;

  /* What kind of socket this is.  */
  struct pipe_class *pipe_class;

  /* Reads from this socket come from READ_PIPE, writes go to WRITE_PIPE.
     A sock always has a read pipe, and a write pipe when it's connected to
     another socket.  */
  struct pipe *read_pipe, *write_pipe;

  /* FLAGS from SOCK_*, below.  */
  unsigned flags;

  /* A receive right for this socket's id ports.  */
  mach_port_t id;

  /* Last time the socket got frobbed.  */
  time_value_t change_time;

  /* File mode as reported by stat.  Usually this is S_ISOCK, but it
     should be S_IFIFO for sockets (ab)used in a pipe.  */
  mode_t mode;

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
  struct connq *listen_queue;
  /* A connection queue we're attempting to connect through; a socket may
     only be attempting one connection at a time.  */
  struct connq *connect_queue;
};

/* Socket flags */
#define PFLOCAL_SOCK_CONNECTED		0x1 /* A connected connection-oriented sock. */
#define PFLOCAL_SOCK_NONBLOCK		0x2 /* Don't block on I/O.  */
#define PFLOCAL_SOCK_SHUTDOWN_READ	0x4 /* The read-half has been shutdown.  */
#define PFLOCAL_SOCK_SHUTDOWN_WRITE	0x8 /* The write-half has been shutdown.  */

/* Returns the pipe that SOCK is reading from in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  NULL may
   also be returned in PIPE with a 0 error, meaning that EOF should be
   returned.  SOCK mustn't be locked.  */
error_t sock_acquire_read_pipe (struct sock *sock, struct pipe **pipe);

/* Returns the pipe that SOCK is writing to in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t sock_acquire_write_pipe (struct sock *sock, struct pipe **pipe);

/* Connect together the previously unconnected sockets SOCK1 and SOCK2.  */
error_t sock_connect (struct sock *sock1, struct sock *sock2);

/* Return a new socket with the given pipe class in SOCK.  */
error_t sock_create (struct pipe_class *pipe_class, mode_t mode,
		     struct sock **sock);

/* Free SOCK, assuming there are no more handle on it.  */
void sock_free (struct sock *sock);

/* Free a sock derefed too far.  */
void _sock_norefs (struct sock *sock);

/* Bind SOCK to ADDR.  */
error_t sock_bind (struct sock *sock, struct addr *addr);

/* Remove a reference from SOCK, possibly freeing it.  */
static inline void __attribute__ ((unused))
sock_deref (struct sock *sock)
{
  error_t err;
  pthread_mutex_lock (&sock->lock);

  sock->refs--;

  if (sock->refs == 0)
    _sock_norefs (sock);
  else if (sock->refs == 1 && sock->addr)
    {
      /* Last ref is the address, there won't be any more port for this socket,
         unbind SOCK from its addr, and they will all die.  */

      /* Keep another ref while unbinding.  */
      sock->refs++;
      pthread_mutex_unlock (&sock->lock);

      /* Unbind */
      err = sock_bind (sock, NULL);
      assert_backtrace (!err);

      /* And release the ref, and thus kill SOCK.  */
      pthread_mutex_lock (&sock->lock);
      sock->refs--;
      assert_backtrace (sock->refs == 0);
      _sock_norefs (sock);
    }
  else
    pthread_mutex_unlock (&sock->lock);
}

/* Return a new socket just like TEMPLATE in SOCK.  */
error_t sock_clone (struct sock *template, struct sock **sock);

/* Return a new user port on SOCK in PORT.  */
error_t sock_create_port (struct sock *sock, mach_port_t *port);

/* Returns SOCK's address in ADDR, with an additional reference added.  If
   SOCK doesn't currently have an address, one is fabricated first.  */
error_t sock_get_addr (struct sock *sock, struct addr **addr);

/* If SOCK is a connected socket, returns a send right to SOCK's peer's
   address in ADDR_PORT.  */
error_t sock_get_write_addr_port (struct sock *sock, mach_port_t *addr_port);

/* Shutdown either the read or write halves of SOCK, depending on whether the
   PFLOCAL_SOCK_SHUTDOWN_READ or PFLOCAL_SOCK_SHUTDOWN_WRITE flags are set in FLAGS.  */
void sock_shutdown (struct sock *sock, unsigned flags);

/* Return a new address, not connected to any socket yet, ADDR.  */
error_t addr_create (struct addr **addr);

/* Returns the socket bound to ADDR in SOCK, or EADDRNOTAVAIL.  The returned
   sock will have one reference added to it.  */
error_t addr_get_sock (struct addr *addr, struct sock **sock);

/* Prepare for socket creation.  */
error_t sock_global_init ();

/* Try to shutdown any active sockets, returning EBUSY if we can't.  Assumes
   non-socket RPCS's have been disabled.  */
error_t sock_global_shutdown ();

/* Mostly here for use by mig-decls.h.  */
extern struct port_class *sock_user_port_class;
extern struct port_class *addr_port_class;

#endif /* __SOCK_H__ */
