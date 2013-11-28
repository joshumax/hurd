/*
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

/* Only CPP macro definitions should go in this file. */

#define IO_SELECT_REPLY_PORT

#define IO_INTRAN sock_user_t begin_using_socket_port (io_t)
#define IO_INTRAN_PAYLOAD sock_user_t begin_using_socket_payload
#define IO_DESTRUCTOR end_using_socket_port (sock_user_t)
#define IO_IMPORTS import "mig-decls.h";
#define IIOCTL_IMPORTS import "mig-decls.h";

#define SOCKET_INTRAN sock_user_t begin_using_socket_port (socket_t)
#define SOCKET_INTRAN_PAYLOAD sock_user_t begin_using_socket_payload
#define SOCKET_DESTRUCTOR end_using_socket_port (sock_user_t)
#define SOCKET_IMPORTS				\
  import "mig-decls.h";				\
  import "../libtrivfs/mig-decls.h";		\

#define ADDRPORT_INTRAN sock_addr_t begin_using_sockaddr_port (addr_port_t)
#define ADDRPORT_INTRAN_PAYLOAD sock_addr_t begin_using_sockaddr_payload
#define ADDRPORT_DESTRUCTOR end_using_sockaddr_port (sock_addr_t)

#define PF_INTRAN trivfs_protid_t trivfs_begin_using_protid (pf_t)
#define PF_INTRAN_PAYLOAD trivfs_protid_t trivfs_begin_using_protid_payload
#define PF_DESTRUCTOR trivfs_end_using_protid (trivfs_protid_t)
