/* libdiskfs implementation of fs.defs: file_seek
   Copyright (C) 1992, 1993, 1994, 1995, 1996 Free Software Foundation

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
#include "fs_S.h"

/* Implement file_sync as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_sync (struct protid *cred,
		    int wait,
		    int omitmetadata)
{
  struct node *np;

  if (!cred)
    return EOPNOTSUPP;
  
  if (diskfs_synchronous)
    wait = 1;

  np = cred->po->np;
  
  pthread_mutex_lock (&np->lock);
  iohelp_get_conch (&np->conch);
  pthread_mutex_unlock (&np->lock);
  diskfs_file_update (np, wait);
  return 0;
}
