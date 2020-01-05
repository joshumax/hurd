/*
   Copyright (C) 1995,2000,02 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#define _HACK_ERRNO_H
#include <assert-backtrace.h>
#include "pfinet.h"

#include <linux/socket.h>
#include <linux/net.h>

#ifndef NPROTO
#define NPROTO (PF_INET + 1)
#endif
struct net_proto_family *net_families[NPROTO];

/* Notice that a protocol family is live; this only works for inet here. */
int
sock_register (struct net_proto_family *fam)
{
  assert_backtrace (fam->family < NPROTO);
  net_families[fam->family] = fam;
  return 0;
}


struct socket *
sock_alloc (void)
{
  static ino_t nextino;		/* locked by global_lock */
  struct socket *sock;
  pthread_cond_t *c;

  sock = malloc (sizeof *sock + sizeof (pthread_cond_t));
  if (!sock)
    return 0;
  c = (void *) &sock[1];
  pthread_cond_init (c, NULL);
  memset (sock, 0, sizeof *sock);
  sock->state = SS_UNCONNECTED;
  sock->identity = MACH_PORT_NULL;
  sock->refcnt = 1;
  sock->wait = (void *) c;

  if (nextino == 0)
    nextino = 2;
  sock->st_ino = nextino++;

  return sock;
}

/* Create a sock_user structure, initialized from SOCK and ISROOT.
   If NOINSTALL is set, don't put it in the portset.
   We increment SOCK->refcnt iff CONSUME is zero.  */
struct sock_user *
make_sock_user (struct socket *sock, int isroot, int noinstall, int consume)
{
  error_t err;
  struct sock_user *user;

  assert_backtrace (sock->refcnt != 0);

  if (noinstall)
    err = ports_create_port_noinstall (socketport_class, pfinet_bucket,
				       sizeof (struct sock_user), &user);
  else
    err = ports_create_port (socketport_class, pfinet_bucket,
			     sizeof (struct sock_user), &user);
  if (err)
    {
      errno = err;
      return 0;
    }

  /* We maintain a reference count in `struct socket' (a member not
     in the original Linux structure), because there can be multiple
     ports (struct sock_user, aka protids) pointing to the same socket.
     The socket lives until all the ports die.  */
  if (! consume)
    ++sock->refcnt;
  user->isroot = isroot;
  user->sock = sock;
  return user;
}

/* This is called from the port cleanup function below, and on
   a newly allocated socket when something went wrong in its creation.  */
void
sock_release (struct socket *sock)
{
  if (--sock->refcnt != 0)
    return;

  if (sock->state != SS_UNCONNECTED)
    sock->state = SS_DISCONNECTING;

  if (sock->ops)
    sock->ops->release(sock, NULL);

  if (sock->identity != MACH_PORT_NULL)
    mach_port_destroy (mach_task_self (), sock->identity);

  free (sock);
}

/* Release the reference on the referenced socket. */
void
clean_socketport (void *arg)
{
  struct sock_user *const user = arg;

  pthread_mutex_lock (&global_lock);
  sock_release (user->sock);
  pthread_mutex_unlock (&global_lock);
}
