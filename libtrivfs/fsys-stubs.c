/*
   Copyright (C) 1994,95,96,2002 Free Software Foundation, Inc.

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
#include "trivfs_fsys_S.h"

kern_return_t __attribute__((weak))
trivfs_S_fsys_startup (mach_port_t bootport,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       int flags,
		       mach_port_t cntl,
		       mach_port_t *realnode,
		       mach_msg_type_name_t *realnodetype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_fsys_getpriv (struct trivfs_control *cntl,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       mach_port_t *host, mach_msg_type_name_t *host_privPoly,
		       mach_port_t *dev, mach_msg_type_name_t *devPoly,
		       mach_port_t *fstask, mach_msg_type_name_t *fstPoly)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_fsys_init (struct trivfs_control *control,
		    mach_port_t reply,
		    mach_msg_type_name_t replytype,
		    mach_port_t proc,
		    auth_t auth)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_fsys_getfile (struct trivfs_control *cntl,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       uid_t *genuids,
		       size_t ngenuids,
		       uid_t *gengids,
		       size_t ngengids,
		       data_t handle,
		       size_t handlesize,
		       mach_port_t *file,
		       mach_msg_type_name_t *filetype)
{
  return EOPNOTSUPP;
}
