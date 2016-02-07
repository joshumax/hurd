/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include "netfs.h"

struct protid *
netfs_make_protid (struct peropen *po, struct iouser *cred)
{
  struct protid *pi;

  if (cred)
    errno = ports_create_port (netfs_protid_class, netfs_port_bucket, 
			       sizeof (struct protid), &pi);
  else
    errno = ports_create_port_noinstall (netfs_protid_class,
					 netfs_port_bucket, 
					 sizeof (struct protid), &pi);
    
  if (errno)
    return 0;

  pi->po = po;
  pi->user = cred;
  pi->shared_object = MACH_PORT_NULL;
  pi->mapped = 0;
  return pi;
}
