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

#include <unistd.h>
#include "netfs.h"
#include "io_S.h"

error_t
netfs_S_io_seek (struct protid *user,
		 off_t offset,
		 int whence,
		 off_t *newoffset)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&user->po->np->lock);
  switch (whence)
    {
    case SEEK_SET:
      err = 0;
      user->po->filepointer = offset;
      break;

    case SEEK_CUR:
      err = 0;
      user->po->filepointer += offset;
      break;
      
    case SEEK_END:
      err = netfs_validate_stat (user->po->np, user->credential);
      if (!err)
	user->po->filepointer = user->po->np->nn_stat.st_size + offset;
      break;
      
    default:
      err = EINVAL;
      break;
    }
  *newoffset = user->po->filepointer;
  mutex_unlock (&user->po->np->lock);
  return err;
}

