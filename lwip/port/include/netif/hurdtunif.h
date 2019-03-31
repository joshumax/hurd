/*
   Copyright (C) 2017 Free Software Foundation, Inc.
   Written by Joan Lledó.

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

/* Tunnel devices module */

#ifndef LWIP_HURDTUNIF_H
#define LWIP_HURDTUNIF_H

#include <hurd/ports.h>

#include <lwip/netif.h>
#include <netif/ifcommon.h>

/* Queue of data in the tunnel */
struct pbufqueue
{
  struct pbuf *head;
  struct pbuf **tail;
  uint8_t len;
};

/* Extension of the common device interface to store tunnel metadata */
struct hurdtunif
{
  struct ifcommon comm;

  struct trivfs_control *cntl;	/* Identify the tunnel device in use */
  file_t underlying;		/* Underlying node where the tunnel is bound */
  struct iouser *user;		/* Restrict the access to one user at a time */
  struct pbufqueue queue;	/* Output queue */

  /* Concurrent access to the queue */
  pthread_mutex_t lock;
  pthread_cond_t read;
  pthread_cond_t select;
  uint8_t read_blocked;
};

struct port_class *tunnel_cntlclass;
struct port_class *tunnel_class;

/* Device initialization */
err_t hurdtunif_device_init (struct netif *netif);

/* Module initialization */
error_t hurdtunif_module_init ();

#endif /* LWIP_HURDTUNIF_H */
