/*
   Copyright (C) 1994 Free Software Foundation

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

error_t
trivfs_S_fsys_goaway (mach_port_t fsys,
		      int flags)
{
  error_t err;
  struct port_info *pi;
  
  pi = ports_get_port (fsys, trivfs_cntl_porttype);
  if (!pi)
    return EOPNOTSUPP;

  err = trivfs_goaway (flags);
  ports_done_with_port (pi);
  return err;
}
