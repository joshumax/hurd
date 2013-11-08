/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "netfs.h"
#include "fs_S.h"

error_t
netfs_S_dir_rename (struct protid *fromdiruser, char *fromname,
		    struct protid *todiruser, char *toname, int excl)
{
  error_t err;
  
  if (!fromdiruser)
    return EOPNOTSUPP;

  if (!todiruser)
    return EXDEV;
  
  /* Note that nothing is locked here */
  err = netfs_attempt_rename (fromdiruser->user, fromdiruser->po->np, 
			      fromname, todiruser->po->np, toname, excl);
  if (!err)
    mach_port_deallocate (mach_task_self (), todiruser->pi.port_right);
  return err;
}
