/*
   Copyright (C) 1994, 1996, 1997, 2014-2019 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <sys/file.h>
#include "priv.h"

/* Decrement the reference count on a peropen structure. */
void
diskfs_release_peropen (struct peropen *po)
{
  if (refcount_deref (&po->refcnt) > 0)
    return;

  if (po->root_parent)
    mach_port_deallocate (mach_task_self (), po->root_parent);

  if (po->shadow_root && po->shadow_root != po->np)
    diskfs_nrele (po->shadow_root);

  if (po->shadow_root_parent)
    mach_port_deallocate (mach_task_self (), po->shadow_root_parent);
  fshelp_rlock_drop_peropen (&po->lock_status);
  if (fshelp_rlock_peropen_status(&po->lock_status) != LOCK_UN)
    diskfs_nput (po->np);
  else
    diskfs_nrele (po->np);
  fshelp_rlock_po_fini (&po->lock_status);

  free (po->path);
  free (po);
}
