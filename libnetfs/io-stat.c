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
#include "io_S.h"
#include <string.h>

error_t
netfs_S_io_stat (struct protid *fileuser,
		 io_statbuf_t *statbuf)
{
  error_t err;
  
  if (!fileuser)
    return EOPNOTSUPP;

  mutex_lock (&fileuser->po->np->lock);
  err = netfs_validate_stat (fileuser->po->np, fileuser->user);
  if (!err)
    bcopy (&fileuser->po->np->nn_stat, statbuf, sizeof (struct stat));
  mutex_unlock (&fileuser->po->np->lock);
  return err;
}

