/* 
   Copyright (C) 1994, 1995, 1996, 2001 Free Software Foundation

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
#include "diskfs.h"
#include "priv.h"
#include "io_S.h"

/* Implement io_prenotify as described in <hurd/io.defs>. 

   We set the prenotify size to be the allocated size of the file;
   then users are forced to call this routine before writing past
   that, and we can do allocation (or return ENOSPC if necessary). */
kern_return_t
diskfs_S_io_prenotify (struct protid *cred,
		       vm_offset_t start __attribute__ ((unused)),
		       vm_offset_t end)
{
  struct node *np;
  int err = 0;
  diskfs_transaction_t *txn;
  if (!cred)
    return EOPNOTSUPP;

  if (diskfs_check_readonly ())
    return EROFS;

  np = cred->po->np;

  txn = diskfs_journal_start_transaction ();
  /* Clamp it down */
  pthread_mutex_lock (&np->lock);

  if (!cred->mapped)
    {
      err = EINVAL;
      goto out;
    }

  err = iohelp_verify_user_conch (&np->conch, cred);
  if (err)
    goto out;
  
  iohelp_fetch_shared_data (cred);
  
  if ((off_t) end < np->allocsize)
    {
      /* The user didn't need to do this, so we'll make sure they
	 have the right shared page info.  */
      pthread_spin_lock (&cred->mapped->lock);
      iohelp_put_shared_data (cred);
      pthread_spin_unlock (&cred->mapped->lock);
      /* Stop the empty transaction and return cleanly! */
      pthread_mutex_unlock (&np->lock);
      diskfs_journal_stop_transaction (txn);
      return 0;
    }
  
  err = diskfs_grow (np, end, cred);
  diskfs_node_update (np, diskfs_synchronous);
  if (!err && np->filemod_reqs)
    diskfs_notice_filechange (np, FILE_CHANGED_EXTEND, 0, end);
 out:
  pthread_mutex_unlock (&np->lock);
  if (!err && (diskfs_synchronous || diskfs_journal_needs_sync (txn)))
    diskfs_journal_commit_transaction (txn);
  else
    diskfs_journal_stop_transaction (txn);
  return err;
}
