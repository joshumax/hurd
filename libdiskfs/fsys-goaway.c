/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"

/* Implement fsys_goaway as described in <hurd/fsys.defs>. */
error_t
diskfs_S_fsys_goaway (fsys_t controlport,
		      int flags)
{
  struct port_info *pt = ports_lookup_port (diskfs_port_bucket, controlport,
					    diskfs_control_class);
  error_t ret;
  
  if (!pt)
    return EOPNOTSUPP;
  
  /* XXX FSYS_GOAWAY_NOWAIT not implemented. */
  
  ret = diskfs_shutdown (flags);
  ports_port_deref (pt);
  return ret;
}

