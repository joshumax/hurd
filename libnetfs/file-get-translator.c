/*
   Copyright (C) 1996 Free Software Foundation, Inc.
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

#include <string.h>
#include <stdio.h>
#include <hurd/paths.h>
#include "netfs.h"
#include "fs_S.h"

error_t
netfs_S_file_get_translator (struct protid *user,
			     char **trans,
			     mach_msg_type_number_t *translen)
{
  struct node *np;
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  np = user->po->np;
  mutex_lock (&np->lock);
  err = netfs_validate_stat (np, user->credential);

  if (err)
    {
      mutex_unlock (&np->lock);
      return err;
    }

  if (S_ISLNK (np->nn_stat.st_mode))
    {
      unsigned int len = sizeof _HURD_SYMLINK + np->nn_stat.st_size + 1;

      if (len  > *translen)
	vm_allocate (mach_task_self (), (vm_address_t *)trans, len, 1);
      bcopy (_HURD_SYMLINK, *trans, sizeof _HURD_SYMLINK);

      err = netfs_attempt_readlink (user->credential, np,
				    *trans + sizeof _HURD_SYMLINK);
      if (!err)
	{
	  (*trans)[sizeof _HURD_SYMLINK + np->nn_stat.st_size] = '\0';
	  *translen = len;
	}
    }
  else if (S_ISCHR (np->nn_stat.st_mode) || S_ISBLK (np->nn_stat.st_mode))
    {
      char *buf;
      unsigned int buflen;

      buflen = asprintf (&buf, "%s%c%d%c%d",
			 (S_ISCHR (np->nn_stat.st_mode)
			  ? _HURD_CHRDEV
			  : _HURD_BLKDEV),
			 '\0', (np->nn_stat.st_rdev >> 8) & 0377,
			 '\0', (np->nn_stat.st_rdev) & 0377);
      buflen++;			/* terminating nul */

      if (buflen > *translen)
	vm_allocate (mach_task_self (), (vm_address_t *) trans, buflen, 1);
      bcopy (buf, *trans, buflen);
      free (buf);
      *translen = buflen;
      err = 0;
    }
  else if (S_ISFIFO (np->nn_stat.st_mode))
    {
      unsigned int len;

      len = sizeof _HURD_FIFO;
      if (len > *translen)
	vm_allocate (mach_task_self (), (vm_address_t *) trans, len, 1);
      bcopy (_HURD_FIFO, *trans, sizeof _HURD_FIFO);
      *translen = len;
      err = 0;
    }
  else if (S_ISSOCK (np->nn_stat.st_mode))
    {
      unsigned int len;

      len = sizeof _HURD_IFSOCK;
      if (len > *translen)
	vm_allocate (mach_task_self (), (vm_address_t *) trans, len, 1);
      bcopy (_HURD_IFSOCK, *trans, sizeof _HURD_IFSOCK);
      *translen = len;
      err = 0;
    }
  else
    err = EINVAL;

  mutex_unlock (&np->lock);

  return err;
}

