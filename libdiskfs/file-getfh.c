/* Return a file handle (for nfs server support)

   Copyright (C) 1997,99,2002 Free Software Foundation, Inc.

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
diskfs_S_file_getfh (struct protid *cred, data_t *fh, size_t *fh_len)
{
  struct node *node;
  union diskfs_fhandle *f;

  if (! cred)
    return EOPNOTSUPP;

  if (! idvec_contains (cred->user->uids, 0))
    return EPERM;

  assert_backtrace (sizeof *f == sizeof f->bytes);

  node = cred->po->np;

  pthread_mutex_lock (&node->lock);

  if (*fh_len < sizeof (union diskfs_fhandle))
    *fh = mmap (0, sizeof (union diskfs_fhandle), PROT_READ|PROT_WRITE,
		MAP_ANON, 0, 0);
  *fh_len = sizeof *f;

  f = (union diskfs_fhandle *) *fh;

  memset (f, 0, sizeof *f);
  f->data.cache_id = node->cache_id;
  f->data.gen = node->dn_stat.st_gen;

  pthread_mutex_unlock (&node->lock);

  return 0;
}
