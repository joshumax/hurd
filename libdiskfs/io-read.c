/*
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

/* Implement io_read as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_read (struct protid *cred,
		  char **data,
		  mach_msg_type_number_t *datalen,
		  off_t offset,
		  mach_msg_type_number_t maxread)
{
  struct node *np;
  int err;
  int off = offset;
  char *buf;
  int ourbuf = 0;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  if (!(cred->po->openstat & O_READ))
    return EBADF;

  mutex_lock (&np->lock);

  iohelp_get_conch (&np->conch);

  if (off == -1)
    off = cred->po->filepointer;

  if (off > np->dn_stat.st_size)
    maxread = 0;
  else if (off + (off_t) maxread > np->dn_stat.st_size)
    maxread = np->dn_stat.st_size - off;

  if (maxread > *datalen)
    {
      ourbuf = 1;
      vm_allocate (mach_task_self (), (u_int *) &buf, maxread, 1);
      *data = buf;
    }
  else
    buf = *data;

  *datalen = maxread;
  if (maxread)
    err = _diskfs_rdwr_internal (np, buf, off, datalen, 0,
				 cred->po->openstat & O_NOATIME);
  else
    err = 0;
  if (diskfs_synchronous)
    diskfs_node_update (np, 1);	/* atime! */
  if (offset == -1 && !err)
    cred->po->filepointer += *datalen;
  if (err && ourbuf)
    vm_deallocate (mach_task_self (), (u_int) buf, maxread);

  mutex_unlock (&np->lock);
  return err;
}
