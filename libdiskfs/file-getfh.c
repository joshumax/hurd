/* Return a file handle (for nfs server support)

   Copyright (C) 1997 Free Software Foundation

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

#include <string.h>

#include "priv.h"
#include "fs_S.h"
#include "fhandle.h"

/* Return an NFS file handle for CRED in FH & FN_LEN.  */
error_t
diskfs_S_file_getfh (struct protid *cred, char **fh, unsigned *fh_len)
{
  struct node *node;
  struct diskfs_fhandle *f;

  if (! cred)
    return EOPNOTSUPP;

  if (! idvec_contains (cred->user->uids, 0))
    return EPERM;
  
  node = cred->po->np;

  mutex_lock (&node->lock);

  if (*fh_len < sizeof (struct diskfs_fhandle))
    vm_allocate (mach_task_self (), (vm_address_t *) fh, 
		 sizeof (struct diskfs_fhandle), 1);
  *fh_len = sizeof (struct diskfs_fhandle);
  
  f = (struct diskfs_fhandle *)*fh;

  f->cache_id = node->cache_id;
  f->gen = node->dn_stat.st_gen;

  f->filler1 = 0;
  bzero (f->filler2, sizeof f->filler2);

  mutex_unlock (&node->lock);

  return 0;
}
