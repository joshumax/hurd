/* 
   Copyright (C) 1995, 1997 Free Software Foundation, Inc.
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
#include <sys/file.h>

struct peropen *
netfs_make_peropen (struct node *np, int flags, struct peropen *context)
{
  struct peropen *po = malloc (sizeof (struct peropen));
  
  po->filepointer = 0;
  po->lock_status = LOCK_UN;
  po->refcnt = 0;
  po->openstat = flags;
  po->np = np;
  po->path = NULL;

  if (context)
    {
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

      if (context->path)
	{
	  po->path = strdup (context->path);
	  if (! po->path)
	    return ENOMEM;
	}
    }

  netfs_nref (np);

  return po;
}

