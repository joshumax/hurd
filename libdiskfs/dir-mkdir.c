/* libdiskfs implementation of fs.defs: dir_mkdir
   Copyright (C) 1992,93,94,95,96,97,2002 Free Software Foundation, Inc.

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

/* Implement dir_mkdir as found in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_mkdir (struct protid *dircred,
		    char *name,
		    mode_t mode)
{
  struct node *dnp;
  struct node *np = 0;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  int error;

  if (!dircred)
    return EOPNOTSUPP;

  dnp = dircred->po->np;
  if (diskfs_check_readonly ())
    return EROFS;

  pthread_mutex_lock (&dnp->lock);

  error = diskfs_lookup (dnp, name, CREATE, 0, ds, dircred);
  if (error == EAGAIN)
    error = EEXIST;
  if (!error)
    error =  EEXIST;

  if (error != ENOENT)
    {
      diskfs_drop_dirstat (dnp, ds);
      pthread_mutex_unlock (&dnp->lock);
      return error;
    }

  mode &= ~(S_ISPARE | S_IFMT | S_ITRANS);
  mode |= S_IFDIR;

  error = diskfs_create_node (dnp, name, mode, &np, dircred, ds);

  if (diskfs_synchronous)
    {
      diskfs_file_update (dnp, 1);
      diskfs_file_update (np, 1);
    }

  if (!error)
    diskfs_nput (np);

  pthread_mutex_unlock (&dnp->lock);
  return error;
}
