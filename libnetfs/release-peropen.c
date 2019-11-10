/* 
   Copyright (C) 1996, 1997, 2015-2019 Free Software Foundation, Inc.
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

#include <sys/file.h>
#include "netfs.h"

void
netfs_release_peropen (struct peropen *po)
{
  if (refcount_deref (&po->refcnt) > 0)
    return;

  pthread_mutex_lock (&po->np->lock);
  if (po->root_parent)
    mach_port_deallocate (mach_task_self (), po->root_parent);

  if (po->shadow_root && po->shadow_root != po->np)
    {
      pthread_mutex_lock (&po->shadow_root->lock);
      netfs_nput (po->shadow_root);
    }
  if (po->shadow_root_parent)
    mach_port_deallocate (mach_task_self (), po->shadow_root_parent);

  fshelp_rlock_drop_peropen (&po->lock_status);
  fshelp_rlock_po_fini (&po->lock_status);

  netfs_nput (po->np);

  free (po->path);
  free (po);
}
