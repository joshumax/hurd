/* Interface functions for the socket.defs interface.
   Copyright (C) 1995 Free Software Foundation, Inc.
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


#include "socket_S.h"



/* Listen on a socket. */
error_t
S_socket_listen (struct user_sock *user, int queue_limit)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  
  if (user->sock->state == SS_UNCONNECTED)
    {
      if (user->sock->ops && user->sock->ops->listen)
	(*user->sock->ops->listen) (user->sock, queue_limit);
      user->sock->flags |= SO_ACCEPTCON;
      mutex_unlock (&global_lock);
      return 0;
    }
  else
    {
      mutex_unlock (&global_lock);
      return EINVAL;
    }
}

error_t
S_socket_accept (struct sock_user *user,
		 mach_port_t *new_port,
		 mach_msg_type_name_t *new_port_type,
		 mach_port_t *addr_port,
		 mach_msg_type_name_t *addr_port_type)
{
  struct socket *sock, *newsock;
  error_t err;
  int i;

  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  
  sock = user->sock;
  newsock = 0;
  err = 0;

  if ((sock->state != SS_UNCONNECTED)
      || (!(sock->flags & SO_ACCEPTCON)))
    err = EINVAL;
  else if (!(newsock = sock_alloc ()))
    err = ENOSR;
  
  if (err)
    goto out;

  newsock->type = sock->type;
  newsock->ops = sock->ops;
  
  err = - (*sock->ops->dup) (newsock, sock);
  if (err)
    goto out;
  
  /* O_NONBLOCK flag in Linux comes from fd table for third
     arg here...  what to do?  XXX */
  err = - (*sock->ops->accept) (sock, newsock, 0);
  if (err)
    goto out;
  
  make_sockaddr_port (newsock, 1, addr_port, addr_port_type);

  *new_port = ports_get_right (make_sock_user (newsock, sock->isroot));
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;

 out:
  if (err && newsock)
    sock_release (newsock);
  mutex_unlock (&global_lock);
  return err;
}

error_t
S_socket_connect (struct sock_user *user,
		  struct sock_addr *addr)
{
  struct socket *sock;
  error_t err;

  if (!user || !addr)
    return EOPNOTSUPP;
  
  sock = user->sock;
  
  mutex_lock (&global_lock);

  err = 0;
  
  if (sock->state == SS_CONNECTED
      && sock->type != SOCK_DGRAM)
    err = EISCONN;
  else if (sock->state != SS_UNCONNECTED
	   && sock->state != SS_CONNECTING)
    err = EINVAL;
  
  
  if (!err)
    /* Flags here come from fd table in Linux for O_NONBLOCK... XXX */
    err = - (*sock->ops->connect) (sock, addr->address, addr->len, 0);
  
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_socket_bind (struct user_sock *user,
	       struct sock_addr *addr)
{
  error_t err;
  
  if (!sock)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  err = (*user->sock->ops->bind) (user->sock, addr->address, addr->len);
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_socket_name (struct sock_user *user,
	       mach_port_t *addr_port,
	       mach_port_name_t *addr_port_name)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  make_sockaddr_port (user->sock, 0, addr_port, addr_port_name);
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_socket_peername (struct sock_user *user,
		   mach_port_t *addr_port,
		   mach_port_name_t *addr_port_name)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  err = make_sockaddr_port (user->sock, 1, addr_port, addr_port_name);
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_socket_connect2 (struct sock_user *user1,
		   struct sock_user *user2)
{
  error_t err;
  
  if (!user1 || !user2)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);

  if (user1->sock->type != user2->sock->type)
    err = EINVAL;
  else if (user1->sock->state != SS_UNCONNECTED
	   && user2->sock->state != SS_UNCONNECTED)
    err = EISCONN;
  else
    err = - (*user1->sock->ops->socketpair) (user1->sock, user2->sock);
  
  if (!err)
    {
      user1->sock->conn = user2->sock;
      user2->sock->conn = user1->sock;
      user1->sock->state = SS_CONNECTED;
      user2->sock->state = SS_CONNECTED;
    }
  
  mutex_unlock (&global_lock);
  return err;
}

error_t
S_socket_create_address (mach_port_t server,
			 int sockaddr_type,
			 char *data,
			 mach_msg_number_t *data_len,
			 mach_port_t *addr_port,
			 mach_msg_type_name_t *addr_port_type,
			 int binding)
{
  struct sock_addr *addr;
  
  if (sockaddr_type != AF_INET)
    return EAFNOTSUPP;
  
  addr = ports_allocate_port (pfinet_bucket, 
			      sizeof (struct sock_addr) + data_len,
			      addrport_class);
  addr->len = data_len;
  bcopy (data, addr->address, data_len);
  
  *addr_port = ports_get_right (addr);
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

