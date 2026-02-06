/* libdiskfs implementation of fs.defs: dir_mkfile
   Copyright (C) 1994,95,96,97,2002 Free Software Foundation, Inc.

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

#include "diskfs.h"
#include "priv.h"
#include "fs_S.h"
#include <fcntl.h>

/* Implement dir_mkfile as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_mkfile (struct protid *cred,
		     int flags,
		     mode_t mode,
		     mach_port_t *newnode,
		     mach_msg_type_name_t *newnodetype)
{
  struct node *dnp, *np;
  error_t err;
  struct protid *newpi;
  struct peropen *newpo;
  diskfs_transaction_t *txn;

  if (!cred)
    return EOPNOTSUPP;
  if (diskfs_check_readonly ())
    return EROFS;
  dnp = cred->po->np;
  txn = diskfs_journal_start_transaction ();
  pthread_mutex_lock (&dnp->lock);
  if (!S_ISDIR (dnp->dn_stat.st_mode))
    {
      pthread_mutex_unlock (&dnp->lock);
      diskfs_journal_stop_transaction (txn);
      return ENOTDIR;
    }
  err = fshelp_access (&dnp->dn_stat, S_IWRITE, cred->user);
  if (err)
    {
      pthread_mutex_unlock (&dnp->lock);
      diskfs_journal_stop_transaction (txn);
      return err;
    }

  mode &= ~(S_IFMT | S_ISPARE | S_ISVTX | S_ITRANS);
  mode |= S_IFREG;
  err = diskfs_create_node (dnp, 0, mode, &np, cred, 0);
  if (!err)
    diskfs_file_update (dnp, diskfs_synchronous);
  pthread_mutex_unlock (&dnp->lock);
  if (err)
    {
      diskfs_journal_stop_transaction (txn);
      return err;
    }
  diskfs_file_update (np, diskfs_synchronous);

  flags &= ~OPENONLY_STATE_MODES; /* These bits are all meaningless here.  */

  err = diskfs_make_peropen (np, flags, cred->po, &newpo);
  if (! err)
    {
      err = diskfs_create_protid (newpo, cred->user, &newpi);
      if (err)
	diskfs_release_peropen (newpo);
    }

  if (! err)
    {
      *newnode = ports_get_right (newpi);
      *newnodetype = MACH_MSG_TYPE_MAKE_SEND;
      ports_port_deref (newpi);
    }

  if (np)
    diskfs_nput (np);

  if (!err && (diskfs_synchronous || diskfs_journal_needs_sync (txn)))
    diskfs_journal_commit_transaction (txn);
  else
    diskfs_journal_stop_transaction (txn);
  return err;
}
