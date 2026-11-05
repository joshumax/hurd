/*
   Copyright (C) 1994,95,96,97,2001 Free Software Foundation, Inc.

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
#include <fcntl.h>

/* Implement io_write as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_write (struct protid *cred,
                   const_data_t data,
                   mach_msg_type_number_t datalen,
                   off_t offset,
                   vm_size_t *amt)
{
  struct node *np;
  error_t err;
  off_t off = offset;
  mach_msg_type_number_t nwritten;
  int should_sync;
  diskfs_transaction_t *txn;
  int retries = 0;

  if (!cred)
    return EOPNOTSUPP;

  if (diskfs_check_readonly ())
    return EROFS;

  np = cred->po->np;
  if (!(cred->po->openstat & O_WRITE))
    return EBADF;
  should_sync = (cred->po->openstat & O_FSYNC) || diskfs_synchronous;
 retry:
  txn = diskfs_journal_start_transaction ();
  pthread_mutex_lock (&np->lock);

  assert_backtrace (!S_ISDIR(np->dn_stat.st_mode));

  iohelp_get_conch (&np->conch);

  if (off == -1)
    {
      if (cred->po->openstat & O_APPEND)
	cred->po->filepointer = np->dn_stat.st_size;
      off = cred->po->filepointer;
    }
  if (off < 0)
    {
      err = EINVAL;
      goto out;
    }

  while (off + (off_t) datalen > np->allocsize)
    {
      err = diskfs_grow (np, off + datalen, cred);
      diskfs_node_update (np, should_sync);
      if (err)
	goto out;
      if (np->filemod_reqs)
	diskfs_notice_filechange (np, FILE_CHANGED_EXTEND, 0, off + datalen);
    }

  if (off + (off_t) datalen > np->dn_stat.st_size)
    {
      np->dn_stat.st_size = off + datalen;
      np->dn_set_ctime = 1;
      diskfs_node_update (np, should_sync);
    }

  /* The metadata is done; the copy below only faults in pages, whose
     blocks the pager allocates under handles of its own.  A commit waits
     for every open handle, so holding this one through the copy would
     hold every commit, and every RPC waiting for the next transaction,
     behind it.  Release it now unless this write must commit: a commit
     cannot wait under the node lock.  */
  if (txn && !should_sync && !diskfs_journal_needs_sync (txn))
    {
      diskfs_journal_stop_transaction (txn);
      txn = NULL;
    }

  nwritten = datalen;
  err = _diskfs_rdwr_internal (np, (char *) data, off, &nwritten, 1, 0);
  if (!err)
    *amt = nwritten;

  if (!err && offset == -1)
    cred->po->filepointer += nwritten;

  if (!err && should_sync)
    diskfs_file_update (np, should_sync);

  if (!err && np->filemod_reqs)
    diskfs_notice_filechange (np, FILE_CHANGED_WRITE, off, off + nwritten);
 out:
  pthread_mutex_unlock (&np->lock);
  diskfs_journal_end_transaction (txn, !err, should_sync);
  /* Freed blocks can come back once the journal commits.  The write is
     done again from the start, at the same offset: OFF is no longer -1,
     so an append goes where it was going to go.  */
  if (err == ENOSPC && diskfs_journal_should_retry_alloc (&retries))
    goto retry;
  return err;
}
