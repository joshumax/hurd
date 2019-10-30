/*
   Copyright (C) 1995, 1997, 2015-2019 Free Software Foundation, Inc.
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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "netfs.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/file.h>

struct peropen *
netfs_make_peropen (struct node *np, int flags, struct peropen *context)
{
  error_t err;
  struct peropen *po = malloc (sizeof (struct peropen));

  if (!po)
    return NULL;

  po->filepointer = 0;
  err = fshelp_rlock_po_init (&po->lock_status);
  if (err)
    return NULL;
  refcount_init (&po->refcnt, 1);
  po->openstat = flags;
  po->np = np;
  po->path = NULL;

  if (context)
    {
      if (context->path)
	{
	  po->path = strdup (context->path);
	  if (! po->path) {
	    free(po);
	    return NULL;
	  }
	}

      po->root_parent = context->root_parent;
      if (po->root_parent != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), po->root_parent,
			    MACH_PORT_RIGHT_SEND, 1);

      po->shadow_root = context->shadow_root;
      if (po->shadow_root)
	netfs_nref (po->shadow_root);

      po->shadow_root_parent = context->shadow_root_parent;
      if (po->shadow_root_parent != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), po->shadow_root_parent,
			    MACH_PORT_RIGHT_SEND, 1);
    }

  netfs_nref (np);

  return po;
}
