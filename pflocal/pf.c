/* Protocol family operations

   Copyright (C) 1995, 1999, 2000, 2008 Free Software Foundation, Inc.

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

#include <stddef.h>
#include <sys/socket.h>
#include <hurd/pipe.h>

#include "sock.h"

#include "socket_S.h"

/* Create a new socket.  Sock type is, for example, SOCK_STREAM,
   SOCK_DGRAM, or some such.  */
error_t
S_socket_create (mach_port_t pf,
		 int sock_type, int protocol,
		 mach_port_t *port, mach_msg_type_name_t *port_type)
{
  error_t err;
  struct sock *sock;
  struct pipe_class *pipe_class;
  mode_t mode;

  /* We have a set of `magic' protocols that allow the user to choose
     the file type of the socket.  The primary application is to make
     sockets that pretend to be a FIFO, for the implementations of
     pipes.  */
  switch (protocol)
    {
    case 0:
      mode = S_IFSOCK;
      break;
    case S_IFCHR:
    case S_IFSOCK:
    case S_IFIFO:
      mode = protocol;
      break;
    default:
      return EPROTONOSUPPORT;
    }

  switch (sock_type)
    {
    case SOCK_STREAM:
      pipe_class = stream_pipe_class; break;
    case SOCK_DGRAM:
      pipe_class = dgram_pipe_class; break;
    case SOCK_SEQPACKET:
      pipe_class = seqpack_pipe_class; break;
    default:
      return EPROTOTYPE;
    }

  err = sock_create (pipe_class, mode, &sock);
  if (!err)
    {
      err = sock_create_port (sock, port);
      if (err)
	sock_free (sock);
      else
	*port_type = MACH_MSG_TYPE_MAKE_SEND;
    }
  
  return err;
}

error_t
S_socket_create_address (mach_port_t pf, int sockaddr_type,
			 data_t data, size_t data_len,
			 mach_port_t *addr_port,
			 mach_msg_type_name_t *addr_port_type)
{
  return EOPNOTSUPP;
}

error_t
S_socket_fabricate_address (mach_port_t pf,
			    int sockaddr_type,
			    mach_port_t *addr_port,
			    mach_msg_type_name_t *addr_port_type)
{
  error_t err;
  struct addr *addr;

  if (sockaddr_type != AF_LOCAL)
    return EAFNOSUPPORT;

  err = addr_create (&addr);
  if (err)
    return err;

  *addr_port = ports_get_right (addr);
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (addr);

  return 0;
}

/* Implement socket_whatis_address as described in <hurd/socket.defs>.
   Since we cannot tell what our address is, return an empty string as
   the file name.  This is primarily for the implementation of accept
   and recvfrom.  The functions getsockname and getpeername remain
   unsupported for the local namespace.  */
error_t
S_socket_whatis_address (struct addr *addr,
			 int *sockaddr_type,
			 data_t *sockaddr, size_t *sockaddr_len)
{
  socklen_t addr_len = (offsetof (struct sockaddr, sa_data) + 1);
  
  if (! addr)
    return EOPNOTSUPP;

  *sockaddr_type = AF_LOCAL;
  if (*sockaddr_len < addr_len)
    *sockaddr = mmap (0, addr_len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  ((struct sockaddr *) *sockaddr)->sa_len = addr_len;
  ((struct sockaddr *) *sockaddr)->sa_family = *sockaddr_type;
  ((struct sockaddr *) *sockaddr)->sa_data[0] = 0;
  *sockaddr_len = addr_len;
  
  return 0;
}
