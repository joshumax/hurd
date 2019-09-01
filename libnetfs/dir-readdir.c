/*
   Copyright (C) 1996,99,2002 Free Software Foundation, Inc.
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

#include <fcntl.h>

#include "netfs.h"
#include "fs_S.h"

error_t
netfs_S_dir_readdir (struct protid *user,
		     data_t *data,
		     mach_msg_type_number_t *datacnt,
		     boolean_t *data_dealloc,
		     int entry,
		     int nentries,
		     vm_size_t bufsiz,
		     int *amt)
{
  error_t err;
  struct node *np;

  if (!user)
    return EOPNOTSUPP;

  np = user->po->np;
  pthread_mutex_lock (&np->lock);

  err = 0;
  if ((user->po->openstat & O_READ) == 0)
    err = EBADF;
  if (!err)
    err = netfs_validate_stat (np, user->user);
  if (!err && (np->nn_stat.st_mode & S_IFMT) != S_IFDIR)
    err = ENOTDIR;
  if (!err)
    err = netfs_get_dirents (user->user, np, entry, nentries, data,
			     datacnt, bufsiz, amt);
  *data_dealloc = 1;		/* XXX */
  pthread_mutex_unlock (&np->lock);
  return err;
}
