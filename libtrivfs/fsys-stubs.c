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
#include "fsys_S.h"

kern_return_t
trivfs_S_fsys_startup (mach_port_t fsys,
		       mach_port_t cntl,
		       mach_port_t *realnode,
		       mach_port_t *realnodetype,
		       mach_port_t *dotdotnode,
		       mach_port_t *dotdotnodetype)
{
  return EOPNOTSUPP;
}

kern_return_t
trivfs_S_fsys_getpriv (mach_port_t fsys,
		       mach_port_t *host,
		       mach_port_t *dev,
		       mach_port_t *fstask)
{
  return EOPNOTSUPP;
}

kern_return_t
trivfs_S_fsys_init (mach_port_t fsys,
		    mach_port_t proc,
		    mach_msg_type_name_t proctype,
		    mach_port_t auth,
		    mach_msg_type_name_t authtype)
{
  return EOPNOTSUPP;
}

kern_return_t
trivfs_S_fsys_getfile (mach_port_t fsys,
		       uid_t *genuids,
		       u_int ngenuids,
		       uid_t *gengids,
		       u_int ngengids,
		       char *handle,
		       u_int handlesize,
		       mach_port_t *file,
		       mach_msg_type_name_t *filetype)
{
  return EOPNOTSUPP;
}
