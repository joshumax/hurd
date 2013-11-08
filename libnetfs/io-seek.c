/* 
   Copyright (C) 1995, 1996, 2000, 2006 Free Software Foundation, Inc.
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
  error_t err = 0;

  if (!user)
    return EOPNOTSUPP;

  switch (whence)
    {
    case SEEK_CUR:
      offset += user->po->filepointer;
      goto check;
    case SEEK_END:
      {
        struct node *np;

        np = user->po->np;
        pthread_mutex_lock (&np->lock);

        err = netfs_validate_stat (np, user->user);
        if (!err)
	  offset += np->nn_stat.st_size;

        pthread_mutex_unlock (&np->lock);
      }
    case SEEK_SET:
    check:
      if (offset >= 0)
	{
	  *newoffset = user->po->filepointer = offset;
	  break;
	}
    default:
      err = EINVAL;
      break;
    }

  return err;
}
