/* Copyright (C) 1993-1994, 2001, 2014-2019 Free Software Foundation, Inc.

   Written by Neal H Walfield <neal@cs.uml.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "priv.h"
#include "fs_S.h"

#include <fcntl.h>
#include <sys/file.h>

kern_return_t
diskfs_S_file_lock (struct protid *cred, int flags)
{
  error_t err;
  struct flock64 lock;
  struct node *node;
  int openstat;
  mach_port_t rendezvous = MACH_PORT_NULL;

  if (! cred)
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
  openstat = cred->po->openstat;
  if (openstat & (O_RDONLY|O_WRONLY|O_EXEC)) openstat |= O_RDONLY|O_WRONLY;

  node = cred->po->np;
  pthread_mutex_lock (&node->lock);
  err = fshelp_rlock_tweak (&node->userlock, &node->lock,
			    &cred->po->lock_status, openstat,
			    0, 0, flags & LOCK_NB ? F_SETLK64 : F_SETLKW64,
			    &lock, rendezvous);
  pthread_mutex_unlock (&node->lock);
  return err;
}
