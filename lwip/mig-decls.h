/*
   Copyright (C) 1995,96,2000,2017 Free Software Foundation, Inc.
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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __LWIP_MIG_DECLS_H__
#define __LWIP_MIG_DECLS_H__

#include <lwip-hurd.h>

/* MiG bogosity */
typedef struct sock_user *sock_user_t;
typedef struct sock_addr *sock_addr_t;

static inline struct sock_user * __attribute__ ((unused))
begin_using_socket_port (mach_port_t port)
{
  return ports_lookup_port (lwip_bucket, port, socketport_class);
}

static inline struct sock_user * __attribute__ ((unused))
begin_using_socket_payload (unsigned long payload)
{
  return ports_lookup_payload (lwip_bucket, payload, socketport_class);
}

static inline void __attribute__ ((unused))
end_using_socket_port (struct sock_user *user)
{
  if (user)
    ports_port_deref (user);
}

static inline struct sock_addr * __attribute__ ((unused))
begin_using_sockaddr_port (mach_port_t port)
{
  return ports_lookup_port (lwip_bucket, port, addrport_class);
}

static inline struct sock_addr * __attribute__ ((unused))
begin_using_sockaddr_payload (unsigned long payload)
{
  return ports_lookup_payload (lwip_bucket, payload, addrport_class);
}

static inline void __attribute__ ((unused))
end_using_sockaddr_port (struct sock_addr *addr)
{
  if (addr)
    ports_port_deref (addr);
}

#endif /* __LWIP_MIG_DECLS_H__ */
