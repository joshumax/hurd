/* Protocol family operations

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

/* Create a new socket.  Sock type is, for example, SOCK_STREAM,
   SOCK_DGRAM, or some such.  */
S_socket_create (mach_port_t pf,
		 int sock_type, int protocol,
		 mach_port_t *port, mach_msg_type_name_t *port_type)
{
  error_t err;
  struct sock *sock;
  struct sock_user *user;
  struct pipe_class *pipe_class;
  
  if (protocol != 0)
    return EPROTONOSUPPORT;

  switch (sock_type)
    {
    case SOCK_STREAM:
      pipe_class = stream_pipe_class; break;
    case SOCK_DGRAM:
      pipe_class = dgram_pipe_class; break;
    case SOCK_SEQPACKET:
      pipe_class = seqpacket_pipe_class; break;
    default:
      return ESOCKTNOSUPPORT;
    }

  err = sock_create (pipe_class, &sock);
  if (!err)
    err = sock_create_port (sock, port);
  *port_type = MACH_MSG_TYPE_MAKE_SEND;
  
  return err;
}

S_socket_create_address (mach_port_t pf,
			 int sockaddr_type,
			 char *data, unsigned data_len,
			 mach_port_t *addr_port,
			 mach_msg_type_name_t *addr_port_type)
{
  return EOPNOTSUPP;
}

S_socket_fabricate_address (mach_port_t pf,
			    int sockaddr_type,
			    mach_port_t *addr_port,
			    mach_msg_type_name_t *addr_port_type)
{
  error_t err;

  if (sockaddr_type != AF_LOCAL)
    return EAFNOSUPPORT;

  err = addr_create (&addr);
  if (err)
    return err;

  *addr_port = ports_get_right (addr);
  *addr_port_type = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

S_socket_whatis_address (mach_port_t pf,
			 int *sockaddr_type,
			 char **sockaddr, unsigned *sockaddr_len)
{
  return EOPNOTSUPP;
}
