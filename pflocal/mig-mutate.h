/* Automagic type transformation for our mig interfaces

   Copyright (C) 1995 Free Software Foundation, Inc.

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

/* Only CPP macro definitions should go in this file. */

#define IO_SELECT_REPLY_PORT

#define IO_INTRAN sock_user_t begin_using_sock_user_port (io_t)
#define IO_INTRAN_PAYLOAD sock_user_t begin_using_sock_user_payload
#define IO_DESTRUCTOR end_using_sock_user_port (sock_user_t)

#define IO_IMPORTS import "mig-decls.h";

#define FILE_INTRAN sock_user_t begin_using_sock_user_port (io_t)
#define FILE_INTRAN_PAYLOAD sock_user_t begin_using_sock_user_payload
#define FILE_DESTRUCTOR end_using_sock_user_port (sock_user_t)

#define FILE_IMPORTS import "mig-decls.h";

#define SOCKET_INTRAN sock_user_t begin_using_sock_user_port (socket_t)
#define SOCKET_INTRAN_PAYLOAD sock_user_t begin_using_sock_user_payload
#define SOCKET_DESTRUCTOR end_using_sock_user_port (sock_user_t)
#define ADDRPORT_INTRAN addr_t begin_using_addr_port (addr_port_t)
#define ADDRPORT_INTRAN_PAYLOAD addr_t begin_using_addr_payload
#define ADDRPORT_DESTRUCTOR end_using_addr_port (addr_t)

#define SOCKET_IMPORTS import "mig-decls.h";
