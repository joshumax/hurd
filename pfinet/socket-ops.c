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
  if (!user)
    return EOPNOTSUPP;
  
   
