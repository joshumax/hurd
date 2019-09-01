/* 
   Copyright (C) 1995, 1996, 2000 Free Software Foundation, Inc.
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
#include <fcntl.h>

error_t
netfs_S_io_write (struct protid *user,
		  data_t data,
		  mach_msg_type_number_t datalen,
		  off_t offset,
		  mach_msg_type_number_t *amount)
{
  error_t err;
  off_t off = offset;
  struct node *np;
  
  if (!user)
    return EOPNOTSUPP;
  
  if ((user->po->openstat & O_WRITE) == 0)
    return EBADF;

  *amount = datalen;

  np = user->po->np;
  pthread_mutex_lock (&np->lock);

  if (off == -1)
    {
      if (user->po->openstat & O_APPEND)
	{
	  err = netfs_validate_stat (np, user->user);
	  if (err)
	    {
	      pthread_mutex_unlock (&np->lock);
	      return err;
	    }
	  user->po->filepointer = np->nn_stat.st_size;
	}
      off = user->po->filepointer;
    }

  err =  netfs_attempt_write (user->user, np, off, amount, data);
  if (offset == -1 && !err)
    user->po->filepointer += *amount;
  pthread_mutex_unlock (&np->lock);
  
  return err;
}
