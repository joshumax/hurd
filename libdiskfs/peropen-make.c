/* 
   Copyright (C) 1994,97,99,2001,02 Free Software Foundation

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

#include "priv.h"
#include <sys/file.h>

/* Create and return a new peropen structure on node NP with open
   flags FLAGS.  */
error_t
diskfs_make_peropen (struct node *np, int flags, struct peropen *context,
		     struct peropen **ppo)
{
  struct peropen *po = *ppo = malloc (sizeof (struct peropen));

  if (! po)
    return ENOMEM;

  po->filepointer = 0;
  po->lock_status = LOCK_UN;
  refcount_init (&po->refcnt, 0);
  po->openstat = flags;
  po->np = np;
  po->path = NULL;

  if (context)
    {
      if (context->path)
	{
	  po->path = strdup (context->path);
	  if (! po->path)
	    return ENOMEM;
	}

      po->root_parent = context->root_parent;
      if (po->root_parent != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), po->root_parent, 
			    MACH_PORT_RIGHT_SEND, 1);

      po->shadow_root = context->shadow_root;
      if (po->shadow_root)
	diskfs_nref (po->shadow_root);

      po->shadow_root_parent = context->shadow_root_parent;
      if (po->shadow_root_parent != MACH_PORT_NULL)
	mach_port_mod_refs (mach_task_self (), po->shadow_root_parent, 
			    MACH_PORT_RIGHT_SEND, 1);
    }
  else
    {
      po->root_parent = MACH_PORT_NULL;
      po->shadow_root_parent = MACH_PORT_NULL;
      po->shadow_root = _diskfs_chroot_directory ? diskfs_root_node : 0;
    }

  diskfs_nref (np);

  return 0;
}
