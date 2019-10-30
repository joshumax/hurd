/*
   Copyright (C) 1995, 2015-2019 Free Software Foundation, Inc.
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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "netfs.h"
#include "fs_S.h"

#include <fcntl.h>
#include <sys/file.h>

error_t
netfs_S_file_lock (struct protid *user,
		   int flags)
{
  error_t err;
  struct flock64 lock;
  struct node *node;
  int openstat = user->po->openstat;
  mach_port_t rendezvous = MACH_PORT_NULL;

  if (!user)
    return EOPNOTSUPP;

  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;

  if (flags & LOCK_UN)
    lock.l_type = F_UNLCK;
  else if (flags & LOCK_SH)
    lock.l_type = F_RDLCK;
  else if (flags & LOCK_EX)
    lock.l_type = F_WRLCK;
  else
    return EINVAL;

  /*
    XXX: Fix for flock(2) calling fcntl(2)
    From flock(2): A shared or exclusive lock can be placed on a file
    regardless of the mode in which the file was opened.
  */
  if (openstat & (O_RDONLY|O_WRONLY|O_EXEC)) openstat |= O_RDONLY|O_WRONLY;

  node = user->po->np;
  pthread_mutex_lock (&node->lock);
  err = fshelp_rlock_tweak (&node->userlock, &node->lock,
			    &user->po->lock_status, openstat,
			    0, 0, flags & LOCK_NB ? F_SETLK64 : F_SETLKW64,
			    &lock, rendezvous);
  pthread_mutex_unlock (&node->lock);

  return err;
}
