/* 
   Copyright (C) 1994, 1996 Free Software Foundation

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

/* Implement io_set_all_openmodes as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_set_all_openmodes (struct protid *cred,
			       int newbits)
{
  diskfs_transaction_t *txn;

  if (!cred)
    return EOPNOTSUPP;
  
  /* Taking the conch back from a client that mapped the shared page
     can update the node, which writes the inode.  Take the handle before
     the node lock so that start never waits while holding it.  */
  txn = diskfs_journal_start_transaction ();
  pthread_mutex_lock (&cred->po->np->lock);
  iohelp_get_conch (&cred->po->np->conch);
  cred->po->openstat &= ~HONORED_STATE_MODES;
  cred->po->openstat |= (newbits & HONORED_STATE_MODES);
  pthread_mutex_unlock (&cred->po->np->lock);
  diskfs_journal_end_transaction (txn, 1, diskfs_synchronous);
  return 0;
}
