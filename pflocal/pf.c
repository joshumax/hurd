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
  struct socket_user *user;

  if (!cred)
    return EOPNOTSUPP;
  
  if (sock_type != SOCK_DGRAM && sock_type != SOCK_STREAM)
    return ESOCKTNOSUPPORT;
  if (protocol != 0)
    return EPROTONOSUPPORT;
  
  user =
    port_allocate_port (socket_user_bucket,
			sizeof (struct socket_user),
			socket_user_class);
  user->socket = socket;

  *port = ports_get_right (user);
  *port_type = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}
