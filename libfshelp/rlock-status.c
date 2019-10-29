/* Copyright (C) 2001, 2014-2019 Free Software Foundation, Inc.

   Written by Neal H Walfield <neal@cs.uml.edu>

   This file is part of the GNU Hurd.

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

#include "fshelp.h"
#include "rlock.h"
#include <fcntl.h>
#include <sys/file.h>

int fshelp_rlock_peropen_status (struct rlock_peropen *po)
{
  struct rlock_list *l;

  if (! *po->locks)
    return LOCK_UN;

  for (l = *po->locks; l; l = l->po.next)
    if (l->type == F_WRLCK)
      return LOCK_EX;

  return LOCK_SH;
}

/* Like fshelp_rlock_peropen_status except for all users of NODE.  */
int fshelp_rlock_node_status (struct rlock_box *box)
{
  struct rlock_list *l;

  if (! box->locks)
    return LOCK_UN;

  for (l = box->locks; l; l = l->node.next)
    if (l->type == F_WRLCK)
      return LOCK_EX;

  return LOCK_SH;
}

