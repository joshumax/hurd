/*
   Copyright (C) 1995,96,2001 Free Software Foundation, Inc.
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

#ifndef _LIBNETFS_PRIV_H
#define _LIBNETFS_PRIV_H

#include <hurd/hurd_types.h>

#include "netfs.h"

volatile struct mapped_time_value *netfs_mtime;

static inline struct protid * __attribute__ ((unused))
begin_using_protid_port (file_t port)
{
  return ports_lookup_port (netfs_port_bucket, port, netfs_protid_class);
}

static inline struct protid * __attribute__ ((unused))
begin_using_protid_payload (unsigned long payload)
{
  return ports_lookup_payload (netfs_port_bucket, payload, netfs_protid_class);
}

static inline void __attribute__ ((unused))
end_using_protid_port (struct protid *cred)
{
  if (cred)
    ports_port_deref (cred);
}

static inline struct netfs_control * __attribute__ ((unused))
begin_using_control_port (fsys_t port)
{
  return ports_lookup_port (netfs_port_bucket, port, netfs_control_class);
}

static inline struct netfs_control * __attribute__ ((unused))
begin_using_control_payload (unsigned long payload)
{
  return ports_lookup_payload (netfs_port_bucket, payload, netfs_control_class);
}

static inline void __attribute__ ((unused))
end_using_control_port (struct netfs_control *cred)
{
  if (cred)
    ports_port_deref (cred);
}

#endif
