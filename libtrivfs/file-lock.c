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

static struct mutex lock = MUTEX_INITIALIZER;
static struct lock_box lockbox;
static int inited = 0;

error_t
trivfs_S_file_lock (struct protid *cred, int flags)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  mutex_lock (&lock);
  if (!inited)
    {
      fshelp_lock_init (&lockbox);
      inited = 1;
    }
  err = fshelp_acquire_lock (&lockbox, &cred->po->lock_status, &lock, flags);
  mutex_unlock (&lock);
  return err;
}

error_t
trivfs_S_file_lock_stat (struct protid *cred, int *mystatus, int *otherstat)
{
  if (!cred)
    return EOPNOTSUPP;
  
  mutex_lock (&lock);
  *mystatus = cred->po->lock_status;
  *otherstat = lockbox.type;
  mutex_unlock (&lock);
  return 0;
}

