/* Interface functions for the socket.defs interface.
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

#include <hurd/trivfs.h>
#include <string.h>

#include "pfinet.h"
#include "socket_S.h"


error_t
S_socket_create (struct trivfs_protid *master,
		 int sock_type,
		 int protocol,
		 mach_port_t *port,
		 mach_msg_type_name_t *porttype)
{
  struct sock_user *user;
  struct socket *sock;
  error_t err;
  
  if (!master)
    return EOPNOTSUPP;

  /* Don't allow bogus SOCK_PACKET here. */

  if ((sock_type != SOCK_STREAM
       && sock_type != SOCK_DGRAM
       && sock_type != SOCK_SEQPACKET
       && sock_type != SOCK_RAW)
      || protocol < 0)
    return EINVAL;
  
  mutex_lock (&global_lock);

  become_task_protid (master);

  sock = sock_alloc ();
  
  sock->type = sock_type;
  sock->ops = proto_ops;
  
  err = - (*sock->ops->create) (sock, protocol);
  if (err)
    sock_release (sock);
  else
    {
      user = make_sock_user (sock, master->isroot, 0);
      *port = ports_get_right (user);
      *porttype = MACH_MSG_TYPE_MAKE_SEND;
      ports_port_deref (user);
    }
  
  mutex_unlock (&global_lock);

  return err;
}


/* Listen on a socket. */
error_t
S_socket_listen (struct sock_user *user, int queue_limit)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);

  become_task (user);
  
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
  struct sock_user *newuser;
  struct socket *sock, *newsock;
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);

  become_task (user);
  
  sock = user->sock;
  newsock = 0;
  err = 0;

  if ((sock->state != SS_UNCONNECTED)
      || (!(sock->flags & SO_ACCEPTCON)))
    err = EINVAL;
  else if (!(newsock = sock_alloc ()))
    err = ENOMEM;
  
  if (err)
    goto out;

  newsock->type = sock->type;
  newsock->ops = sock->ops;
  
  err = - (*sock->ops->dup) (newsock, sock);
  if (err)
    goto out;
  
  err = - (*sock->ops->accept) (sock, newsock, sock->userflags);
  if (err)
    goto out;
  
  err = make_sockaddr_port (newsock, 1, addr_port, addr_port_type);
  if (err)
    goto out;
  
  newuser = make_sock_user (newsock, user->isroot, 0);
  *new_port = ports_get_right (newuser);
  *new_port_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newuser);

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

  become_task (user);

  err = 0;
  
  if (sock->state == SS_CONNECTED
      && sock->type != SOCK_DGRAM)
    err = EISCONN;
  else if (sock->state != SS_UNCONNECTED
	   && sock->state != SS_CONNECTING
	   && sock->state != SS_CONNECTED)
    err = EINVAL;
  
  if (!err)
    err = - (*sock->ops->connect) (sock, addr->address, addr->len, 
				   sock->userflags);
  
  mutex_unlock (&global_lock);
  
  /* MiG should do this for us, but it doesn't. */
  if (!err)
    mach_port_deallocate (mach_task_self (), addr->pi.port_right);

  return err;
}

error_t
S_socket_bind (struct sock_user *user,
	       struct sock_addr *addr)
{
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);
  err = - (*user->sock->ops->bind) (user->sock, addr->address, addr->len);
  mutex_unlock (&global_lock);
  
  /* MiG should do this for us, but it doesn't. */
  if (!err)
    mach_port_deallocate (mach_task_self (), addr->pi.port_right);

  return err;
}

error_t
S_socket_name (struct sock_user *user,
	       mach_port_t *addr_port,
	       mach_msg_type_name_t *addr_port_name)
{
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);
  make_sockaddr_port (user->sock, 0, addr_port, addr_port_name);
  mutex_unlock (&global_lock);
  return 0;
}

error_t
S_socket_peername (struct sock_user *user,
		   mach_port_t *addr_port,
		   mach_msg_type_name_t *addr_port_name)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);
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

  become_task (user1);

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

  /* MiG should do this for us, but it doesn't. */
  if (!err)
    mach_port_deallocate (mach_task_self (), user2->pi.port_right);

  return err;
}

error_t
S_socket_create_address (mach_port_t server,
			 int sockaddr_type,
			 char *data,
			 mach_msg_type_number_t data_len,
			 mach_port_t *addr_port,
			 mach_msg_type_name_t *addr_port_type)
{
  struct sock_addr *addr;
  error_t err;
  
  if (sockaddr_type != AF_INET)
    return EAFNOSUPPORT;
  
  err = ports_create_port (addrport_class, pfinet_bucket, 
			   sizeof (struct sock_addr) + data_len, &addr);
  if (err)
    return err;
  
  addr->len = data_len;
  bcopy (data, addr->address, data_len);
  
  *addr_port = ports_get_right (addr);
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (addr);
  return 0;
}

error_t
S_socket_fabricate_address (mach_port_t server,
			    int sockaddr_type,
			    mach_port_t *addr_port,
			    mach_msg_type_name_t *addr_port_type)
{
  return EOPNOTSUPP;
}

error_t
S_socket_whatis_address (struct sock_addr *addr,
			 int *type,
			 char **data,
			 mach_msg_type_number_t *datalen)
{
  if (!addr)
    return EOPNOTSUPP;
  
  *type = AF_INET;
  if (*datalen < addr->len)
    vm_allocate (mach_task_self (), (vm_address_t *) data, addr->len, 1);
  bcopy (addr->address, *data, addr->len);
  *datalen = addr->len;

  return 0;
}

error_t
S_socket_shutdown (struct sock_user *user,
		   int direction)
{
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);
  err = - (*user->sock->ops->shutdown) (user->sock, direction);
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_socket_getopt (struct sock_user *user,
		 int level,
		 int option,
		 char **data,
		 u_int *datalen)
{
  return EOPNOTSUPP;
}

error_t
S_socket_setopt (struct sock_user *user,
		 int level,
		 int option,
		 char *data,
		 u_int datalen)
{
  error_t err;

  if (! user)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
  become_task (user);

  err =
    - (user->sock->ops->setsockopt)(user->sock, level, option, data, datalen);

  mutex_unlock (&global_lock);

  return err;
}

error_t
S_socket_send (struct sock_user *user,
	       struct sock_addr *addr,
	       int flags,
	       char *data,
	       u_int datalen,
	       mach_port_t *ports,
	       u_int nports,
	       char *control,
	       u_int controllen,
	       mach_msg_type_number_t *amount)
{
  int sent;
  
  if (!user)
    return EOPNOTSUPP;
  
  /* Don't do this yet, it's too bizarre to think about right now. */
  if (nports != 0 || controllen != 0)
    return EINVAL;
  
  mutex_lock (&global_lock);

  become_task (user);
  
  if (addr)
    sent = (*user->sock->ops->sendto) (user->sock, data, datalen, 
				       user->sock->userflags, flags,
				       addr->address, addr->len);
  else
    sent = (*user->sock->ops->send) (user->sock, data, datalen, 
				     user->sock->userflags, flags);
  
  mutex_unlock (&global_lock);
  
  /* MiG should do this for us, but it doesn't. */
  if (sent >= 0)
    mach_port_deallocate (mach_task_self (), addr->pi.port_right);

  if (sent >= 0)
    {
      *amount = sent;
      return 0;
    }
  else
    return (error_t)-sent;
}

error_t
S_socket_recv (struct sock_user *user,
	       mach_port_t *addrport,
	       mach_msg_type_name_t *addrporttype,
	       int flags,
	       char **data,
	       u_int *datalen,
	       mach_port_t **ports,
	       mach_msg_type_name_t *portstype,
	       u_int *nports,
	       char **control,
	       u_int *controllen,
	       int *outflags,
	       mach_msg_type_number_t amount)
{
  int recvd;
  char addr[128];
  size_t addrlen = sizeof addr;
  int didalloc = 0;

  if (!user)
    return EOPNOTSUPP;
  
  /* For unused recvmsg interface */
  *nports = 0;
  *portstype = MACH_MSG_TYPE_COPY_SEND;
  *controllen = 0;
  *outflags = 0;

  /* Instead of this, we should peek at the socket and only allocate
     as much as necessary. */
  if (*datalen < amount)
    {
      vm_allocate (mach_task_self (), (vm_address_t *) data, amount, 1);
      didalloc = 1;
    }
  
  mutex_lock (&global_lock);
  become_task (user);

  recvd =  (*user->sock->ops->recvfrom) (user->sock, *data, amount, 0, flags,
					 (struct sockaddr *)addr, &addrlen);
  
  mutex_unlock (&global_lock);

  if (recvd < 0)
    return (error_t)-recvd;

  *datalen = recvd;

  if (didalloc && round_page (*datalen) < round_page (amount))
    vm_deallocate (mach_task_self (), 
		   (vm_address_t) (*data + round_page (*datalen)),
		   round_page (amount) - round_page (*datalen));

  
  S_socket_create_address (0, AF_INET, addr, addrlen, addrport, 
			   addrporttype);
  
  return 0;
}

