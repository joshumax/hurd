/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include "pfinet.h"
#include <string.h>

/* Create a sock_user structure, initialized from SOCK and ISROOT.
   If NOINSTALL is set, don't put it in the portset.  */
struct sock_user *
make_sock_user (struct socket *sock, int isroot, int noinstall)
{
  struct sock_user *user;
  
  if (noinstall)
    errno = ports_create_port_noinstall (socketport_class, pfinet_bucket,
					 sizeof (struct sock_user), &user);
  else
    errno = ports_create_port (socketport_class, pfinet_bucket,
			       sizeof (struct sock_user), &user);
  if (errno)
    return 0;
  
  user->isroot = isroot;
  user->sock = sock;
  sock->refcnt++;
  return user;
}

/* Create a sockaddr port.  Fill in *ADDR and *ADDRTYPE accordingly.
   The address should come from SOCK; PEER is 0 if we want this socket's 
   name and 1 if we want the peer's name. */
error_t
make_sockaddr_port (struct socket *sock, 
		    int peer,
		    mach_port_t *addr,
		    mach_msg_type_name_t *addrtype)
{
  char buf[128];
  int buflen = 128;
  error_t err;
  struct sock_addr *addrstruct;

  err = (*sock->ops->getname) (sock, (struct sockaddr *)buf, &buflen, peer);
  if (err)
    return err;
  
  err = ports_create_port (addrport_class, pfinet_bucket,
			   sizeof (struct sock_addr) + buflen, &addrstruct);
  if (err)
    return err;
  addrstruct->len = buflen;
  bcopy (buf, addrstruct->address, buflen);
  *addr = ports_get_right (addrstruct);
  *addrtype = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (addrstruct);
  return 0;
}

struct sock_user *
begin_using_socket_port (mach_port_t port)
{
  return ports_lookup_port (pfinet_bucket, port, socketport_class);
}

void
end_using_socket_port (struct sock_user *user)
{
  if (user)
    ports_port_deref (user);
}

struct sock_addr *
begin_using_sockaddr_port (mach_port_t port)
{
  return ports_lookup_port (pfinet_bucket, port, addrport_class);
}

void
end_using_sockaddr_port (struct sock_addr *addr)
{
  if (addr)
    ports_port_deref (addr);
}

/* Nothing need be done here. */
void
clean_addrport (void *arg)
{
}

/* Release the reference on the referenced socket. */
void
clean_socketport (void *arg)
{
  struct sock_user *user = arg;
  
  mutex_lock (&global_lock);
  
  user->sock->refcnt--;
  if (user->sock->refcnt == 0)
    sock_release (user->sock);

  mutex_unlock (&global_lock);
}

struct socket *
sock_alloc (void)
{
  struct socket *sock;
  struct wait_queue *wait, **waitp;

  sock = malloc (sizeof (struct wait_queue) 
		 + sizeof (struct wait_queue *)
		 + sizeof (struct socket));
  wait = (void *)sock + sizeof (struct socket);
  waitp = (void *)wait + sizeof (struct wait_queue);
  
  bzero (sock, sizeof (struct socket));
  sock->identity = MACH_PORT_NULL;
  sock->state = SS_UNCONNECTED;
  sock->wait = waitp;

  condition_init (&wait->c);

  *waitp = wait;

  return sock;
}

static inline void sock_release_peer(struct socket *peer)
{
	peer->state = SS_DISCONNECTING;
	wake_up_interruptible(peer->wait);
	sock_wake_async(peer, 1);
}

void
sock_release (struct socket *sock)
{
  	int oldstate;
	struct socket *peersock, *nextsock;

	if ((oldstate = sock->state) != SS_UNCONNECTED)
		sock->state = SS_DISCONNECTING;

	/*
	 *	Wake up anyone waiting for connections. 
	 */

	for (peersock = sock->iconn; peersock; peersock = nextsock) 
	{
		nextsock = peersock->next;
		sock_release_peer(peersock);
	}

	/*
	 * Wake up anyone we're connected to. First, we release the
	 * protocol, to give it a chance to flush data, etc.
	 */

	peersock = (oldstate == SS_CONNECTED) ? sock->conn : NULL;
	if (sock->ops) 
		(*sock->ops->release) (sock, peersock);
	if (peersock)
		sock_release_peer(peersock);

	if (sock->identity != MACH_PORT_NULL)
	  mach_port_destroy (mach_task_self (), sock->identity);
	free (sock);
}

  
