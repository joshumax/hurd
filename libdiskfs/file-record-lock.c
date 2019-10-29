/* Copyright (C) 2001, 2014-2019 Free Software Foundation, Inc.

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
#include "diskfs.h"
#include "fs_S.h"

#include <errno.h>
#include <hurd/fshelp.h>

error_t
diskfs_S_file_record_lock (struct protid *cred,
			   int cmd,
			   struct flock64 *lock,
			   mach_port_t rendezvous)
{
  struct node *node;
  error_t err;

  if (! cred)
    return EOPNOTSUPP;

  node = cred->po->np;
  pthread_mutex_lock (&node->lock);
  err = fshelp_rlock_tweak (&node->userlock, &node->lock,
			    &cred->po->lock_status, cred->po->openstat,
			    node->dn_stat.st_size, cred->po->filepointer,
			    cmd, lock, rendezvous);
  pthread_mutex_unlock (&node->lock);
  return err;
}
