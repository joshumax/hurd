/*
   Copyright (C) 1993,94,96,99,2002 Free Software Foundation, Inc.

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

#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>

#include <mach.h>
#include "priv.h"
#include <hurd/hurd_types.h>
#include "fs_S.h"

kern_return_t
diskfs_S_dir_readdir (struct protid *cred,
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
  diskfs_transaction_t *txn;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  /* Reading the directory updates its access time, which writes the
     inode.  Take the handle before the node lock so that start never
     waits while holding it.  */
  txn = diskfs_journal_start_transaction ();
  pthread_mutex_lock (&np->lock);

  if ((cred->po->openstat & O_READ) == 0)
    {
      pthread_mutex_unlock (&np->lock);
      diskfs_journal_stop_transaction (txn);
      return EBADF;
    }

  if ((np->dn_stat.st_mode & S_IFMT) != S_IFDIR)
    {
      pthread_mutex_unlock (&np->lock);
      diskfs_journal_stop_transaction (txn);
      return ENOTDIR;
    }

  err = diskfs_get_directs (np, entry, nentries, data, datacnt, bufsiz, amt);
  *data_dealloc = 1;		/* XXX */
  pthread_mutex_unlock (&np->lock);
  diskfs_journal_end_transaction (txn, 1, diskfs_synchronous);
  return err;
}
