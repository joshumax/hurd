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

S_socket_create (struct trivfs_protid *cred,
		 int sock_type, int protocol,
		 mach_port_t *port, mach_msg_type_name_t *port_type)
{
  error_t err;
  struct pipe_ops *pipe_ops;
  struct sock *sock;
  struct sock_user *user;

  if (!cred)
    return EOPNOTSUPP;
  
  if (protocol != 0)
    return EPROTONOSUPPORT;
  switch (sock_type)
    {
    case SOCK_STREAM:
      pipe_ops = stream_pipe_ops; break;
    case SOCK_DGRAM:
      pipe_ops = dgram_pipe_ops; break;
    default:
      return ESOCKTNOSUPPORT;
    }
  
  err = sock_create (pipe_ops, &sock);
  if (!err)
    err = sock_create_port (sock, port, port_type);

  return err;
}
