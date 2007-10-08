/*
   Copyright (C) 1995,96,99,2000,02 Free Software Foundation, Inc.
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

#ifndef PFINET_H_
#define PFINET_H_

#include <device/device.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <sys/mman.h>
#include <sys/socket.h>

extern struct mutex global_lock;
extern struct mutex net_bh_lock;

struct port_bucket *pfinet_bucket;
struct port_class *addrport_class;
struct port_class *socketport_class;

mach_port_t fsys_identity;

extern struct device *dev_base;
extern struct device loopback_dev;

/* A port on SOCK.  Multiple sock_user's can point to the same socket. */
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
  struct sockaddr address;
};

/* Trivfs control structure for pfinet.  */
struct trivfs_control *pfinetctl;

/* Owner of the underlying node.  */
uid_t pfinet_owner;

/* Group of the underlying node.  */
uid_t pfinet_group;

void ethernet_initialize (void);
int ethernet_demuxer (mach_msg_header_t *, mach_msg_header_t *);
void setup_ethernet_device (char *, struct device **);
void setup_dummy_device (char *, struct device **);
void setup_tunnel_device (char *, struct device **);
struct sock_user *make_sock_user (struct socket *, int, int, int);
error_t make_sockaddr_port (struct socket *, int,
			    mach_port_t *, mach_msg_type_name_t *);
void init_devices (void);
any_t net_bh_worker (any_t);
void init_time (void);
void ip_rt_add (short, u_long, u_long, u_long, struct device *,
		u_short, u_long);
void ip_rt_del (u_long, struct device *);
struct sock;
error_t tcp_tiocinq (struct sock *sk, mach_msg_type_number_t *amount);


struct sock_user *begin_using_socket_port (socket_t);
struct sock_addr *begin_using_sockaddr_port (socket_t);
void end_using_socket_port (struct sock_user *);
void end_using_sockaddr_port (struct sock_addr *);
void clean_addrport (void *);
void clean_socketport (void *);

/* MiG bogosity */
typedef struct sock_user *sock_user_t;
typedef struct sock_addr *sock_addr_t;

/* pfinet6 port classes. */
enum {
  PORTCLASS_INET,
  PORTCLASS_INET6,
};

extern struct port_class *trivfs_protid_portclasses[];
extern int trivfs_protid_nportclasses;

extern struct port_class *trivfs_cntl_portclasses[2];
extern int trivfs_cntl_nportclasses;

/* Which portclass to install on the bootstrap port. */
extern int pfinet_bootstrap_portclass;

/* Install portclass on node NAME. */
void pfinet_bind (int portclass, const char *name);

#endif
