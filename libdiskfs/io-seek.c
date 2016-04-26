/*
   Copyright (C) 1994,1995,1996,2000,2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"
#include "io_S.h"
#include <unistd.h>

/* Implement io_seek as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_seek (struct protid *cred,
		  off_t offset,
		  int whence,
		  off_t *newoffset)
{
  error_t err = 0;
  struct node *np;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  iohelp_get_conch (&np->conch);
  switch (whence)
    {
    case SEEK_CUR:
      offset += cred->po->filepointer;
      goto check;
    case SEEK_END:
      offset += np->dn_stat.st_size;
    case SEEK_SET:
    check:
      /* pager_memcpy inherently uses vm_offset_t, which may be smaller than
         off_t.  */
      if (sizeof(off_t) > sizeof(vm_offset_t) &&
	  offset > ((off_t) 1) << (sizeof(vm_offset_t) * 8))
	{
	  err = EFBIG;
	  break;
	}
      if (offset >= 0)
	{
	  *newoffset = cred->po->filepointer = offset;
	  break;
	}
    default:
      err = EINVAL;
      break;
    }

  pthread_mutex_unlock (&np->lock);
  return err;
}
