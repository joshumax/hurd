/* Remount an active filesystem

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"

/* Re-read all incore data structures from disk.  This will only work if
   DISKFS_READONLY is true.  DISKFS_FSYS_LOCK should be held while calling
   this routine.  */
error_t
diskfs_remount ()
{
  error_t err;

  if (! diskfs_check_readonly ())
    return EBUSY;

  err = ports_inhibit_class_rpcs (diskfs_protid_class);
  if (err)
    return err;

  err = diskfs_reload_global_state ();
  if (!err)
    err = diskfs_node_iterate (diskfs_node_reload);

  ports_resume_class_rpcs (diskfs_protid_class);

  return err;
}
