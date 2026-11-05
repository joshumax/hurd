/* 
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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
#include "io_S.h"
#include <fcntl.h>

/* Implement io_readable as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_readable (struct protid *cred,
		      vm_size_t *amount)
{
  struct node *np;
  diskfs_transaction_t *txn;

  if (!cred)
    return EOPNOTSUPP;
  
  if (!(cred->po->openstat & O_READ))
    return EINVAL;

  np = cred->po->np;
  
  /* Taking the conch back from a client that mapped the shared page
     can update the node, which writes the inode.  Take the handle before
     the node lock so that start never waits while holding it.  */
  txn = diskfs_journal_start_transaction ();
  pthread_mutex_lock (&np->lock);
  iohelp_get_conch (&np->conch);
  if (np->dn_stat.st_size > cred->po->filepointer)
    *amount = np->dn_stat.st_size - cred->po->filepointer;
  else
    *amount = 0;

  pthread_mutex_unlock (&np->lock);
  diskfs_journal_end_transaction (txn, 1, diskfs_synchronous);
  return 0;
}
