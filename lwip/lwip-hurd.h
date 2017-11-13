/*
   Copyright (C) 1995, 1996, 1999, 2000, 2002, 2007, 2017
     Free Software Foundation, Inc.

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

/* Translator global declarations */

#ifndef LWIP_HURD_H
#define LWIP_HURD_H

#include <sys/socket.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <refcount.h>

struct port_bucket *lwip_bucket;
struct port_class *socketport_class;
struct port_class *addrport_class;
struct port_class *shutdown_notify_class;

struct port_class *lwip_protid_portclasses[2];
struct port_class *lwip_cntl_portclasses[2];

/* Which portclass to install on the bootstrap port, default to IPv4. */
int lwip_bootstrap_portclass;

mach_port_t fsys_identity;

/* Trivfs control structure for lwip.  */
struct trivfs_control *lwipcntl;

/* Address family port classes. */
enum
{
  PORTCLASS_INET,
  PORTCLASS_INET6,
};

struct socket
{
  int sockno;
  mach_port_t identity;
  refcount_t refcnt;
};

/* Multiple sock_user's can point to the same socket. */
struct sock_user
{
  struct port_info pi;
  int isroot;
  struct socket *sock;
};

/* Socket address ports. */
struct sock_addr
{
  struct port_info pi;
  union
  {
    struct sockaddr_storage storage;
    struct sockaddr sa;
  } address;
};

/* Owner of the underlying node.  */
uid_t lwip_owner;

/* Group of the underlying node.  */
uid_t lwip_group;

struct socket *sock_alloc (void);
void sock_release (struct socket *);

void clean_addrport (void *);
void clean_socketport (void *);

struct sock_user *make_sock_user (struct socket *, int, int, int);
error_t make_sockaddr_port (int, int, mach_port_t *, mach_msg_type_name_t *);

void init_ifs (void *);

/* Install portclass on node NAME. */
void translator_bind (int portclass, const char *name);

#endif
