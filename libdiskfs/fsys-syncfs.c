/* 
   Copyright (C) 1994, 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
#include "fsys_S.h"

/* Implement fsys_syncfs as described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_syncfs (fsys_t controlport,
		      int wait,
		      int children)
{
  struct port_info *pi = ports_check_port_type (controlport, PT_CTL);
  
  if (!pi)
    return EOPNOTSUPP;
  
  if (children)
    diskfs_sync_translators (wait);

  if (diskfs_synchronous)
    wait = 1;
  
  diskfs_sync_everything (wait);
  diskfs_set_hypermetadata (wait, 0);
  return 0;
}

