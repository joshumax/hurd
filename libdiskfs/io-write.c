/* 
   Copyright (C) 1994 Free Software Foundation

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

/* Implement io_write as described in <hurd/io.defs>. */

/* Implement io_write as described in <hurd/io.defs>. */
error_t
S_io_write(struct protid *cred,
	   char *data,
	   unsigned int datalen,
	   off_t offset, 
	   int *amt)
{
  struct node *np;
  error_t err;
  volatile int off = offset;

  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;
  if (!(cred->po->openstat & O_WRITE))
    return EBADF;
  if (*amt < 0)
    return EINVAL;

  mutex_lock (&np->lock);

  assert (!S_ISDIR(np->dn_stat.st_mode));

  err = ioserver_get_conch (&np->conch);
  if (err)
    goto out;
  
  if (off == -1)
    {
      if (cred->po->openstat & O_APPEND)
	cred->po->filepointer = np->dn_stat.st_size;
      off = cred->po->filepointer;
    }
  
  while (off + datalen > np->i_allocsize)
    {
      err = diskfs_grow (np, off + datalen, cred);
      if (err)
	goto out;
    }
      
  if (off + datalen > np->dn_stat.st_size)
    np->dn_stat.st_size = off + datalen;

  if (!err)
    {
      *amt = datalen;
      err = _diskfs_rdwr_internal (np, data, off, datalen, 1);
  
      if (offset == -1)
	cred->po->filepointer += *amt;
    }

 out:
  mutex_unlock (&np->i_toplock);
  return err;
}
