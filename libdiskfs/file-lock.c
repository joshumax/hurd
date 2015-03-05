/*
   Copyright (C) 1993, 1994 Free Software Foundation

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "fs_S.h"

kern_return_t
diskfs_S_file_lock (struct protid *cred, int flags)
{
  error_t err;
  if (!cred)
    return EOPNOTSUPP;
  pthread_mutex_lock (&cred->po->np->lock);
  err = fshelp_acquire_lock (&cred->po->np->userlock, &cred->po->lock_status,
			     &cred->po->np->lock, flags);
  pthread_mutex_unlock (&cred->po->np->lock);
  return err;
}
