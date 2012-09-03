/* 
   Copyright (C) 1994, 1996, 1997 Free Software Foundation

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

#include <sys/file.h>
#include "priv.h"

/* Decrement the reference count on a peropen structure. */
void
diskfs_release_peropen (struct peropen *po)
{
  pthread_mutex_lock (&po->np->lock);

  if (--po->refcnt)
    {
      pthread_mutex_unlock (&po->np->lock);
      return;
    }

  if (po->root_parent)
    mach_port_deallocate (mach_task_self (), po->root_parent);

  if (po->shadow_root && po->shadow_root != po->np)
    diskfs_nrele (po->shadow_root);

  if (po->shadow_root_parent)
    mach_port_deallocate (mach_task_self (), po->shadow_root_parent);

  if (po->lock_status != LOCK_UN)
    fshelp_acquire_lock (&po->np->userlock, &po->lock_status,
			 &po->np->lock, LOCK_UN);

  diskfs_nput (po->np);

  free (po);
}
