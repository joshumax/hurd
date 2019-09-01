/*
   Copyright (C) 1994,95,96,97,2001 Free Software Foundation, Inc.

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
#include <fcntl.h>

/* Implement io_write as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_write (struct protid *cred,
		   data_t data,
		   mach_msg_type_number_t datalen,
		   off_t offset,
		   mach_msg_type_number_t *amt)
{
  struct node *np;
  error_t err;
  off_t off = offset;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  if (!(cred->po->openstat & O_WRITE))
    return EBADF;

  pthread_mutex_lock (&np->lock);

  assert_backtrace (!S_ISDIR(np->dn_stat.st_mode));

  iohelp_get_conch (&np->conch);

  if (off == -1)
    {
      if (cred->po->openstat & O_APPEND)
	cred->po->filepointer = np->dn_stat.st_size;
      off = cred->po->filepointer;
    }
  if (off < 0)
    {
      err = EINVAL;
      goto out;
    }

  while (off + (off_t) datalen > np->allocsize)
    {
      err = diskfs_grow (np, off + datalen, cred);
      if (diskfs_synchronous)
	diskfs_node_update (np, 1);
      if (err)
	goto out;
      if (np->filemod_reqs)
	diskfs_notice_filechange (np, FILE_CHANGED_EXTEND, 0, off + datalen);
    }

  if (off + (off_t) datalen > np->dn_stat.st_size)
    {
      np->dn_stat.st_size = off + datalen;
      np->dn_set_ctime = 1;
      if (diskfs_synchronous)
	diskfs_node_update (np, 1);
    }

  *amt = datalen;
  err = _diskfs_rdwr_internal (np, data, off, amt, 1, 0);

  if (!err && offset == -1)
    cred->po->filepointer += *amt;

  if (!err
      && ((cred->po->openstat & O_FSYNC) || diskfs_synchronous))
    diskfs_file_update (np, 1);

  if (!err && np->filemod_reqs)
    diskfs_notice_filechange (np, FILE_CHANGED_WRITE, off, off + *amt);
 out:
  pthread_mutex_unlock (&np->lock);
  return err;
}
