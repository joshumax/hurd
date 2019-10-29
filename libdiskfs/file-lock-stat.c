/* Copyright (C) 1994-1995, 2001, 2014-2019 Free Software Foundation, Inc.

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
diskfs_S_file_lock_stat (struct protid *cred,
			 int *mystatus,
			 int *otherstatus)
{
  struct node *node;

  if (!cred)
    return EOPNOTSUPP;

  node = cred->po->np;

  pthread_mutex_lock (&node->lock);
  *mystatus = fshelp_rlock_peropen_status (&cred->po->lock_status);
  *otherstatus = fshelp_rlock_node_status (&node->userlock);
  pthread_mutex_unlock (&node->lock);

  return 0;
}
