/* libdiskfs implementation of fs.defs: dir_link
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

/* Implement dir_link as described in <hurd/fs.defs>. */
error_t
diskfs_S_dir_link (struct protid *filecred,
		   struct protid *dircred,
		   char *name)
{
  struct node *np;
  struct node *dnp;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  error_t error;

  if (!filecred)
    return EOPNOTSUPP;
  
  np = filecred->po->np;
  if (diskfs_readonly)
    return EROFS;
  
  if (!dircred)
    return EXDEV;
  
  dnp = dircred->po->np;
  mutex_lock (&dnp->lock);
  /* This lock is safe since a non-directory is inherently a leaf */
  /* XXX But we don't know yet that it is a non-directory */
  mutex_lock (&np->lock);

  if (S_ISDIR (np->dn_stat.st_mode))
    {
      error = EISDIR;
      goto out;
    }
  else if (np->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      error = EMLINK;
      goto out;
    }

  error = diskfs_lookup (dnp, name, CREATE, 0, ds, dircred);

  if (error == EAGAIN)
    error = EEXIST;
  if (!error)
    error = EEXIST;
  if (error != ENOENT)
    {
      diskfs_drop_dirstat (dnp, ds);
      goto out;
    }
  
  np->dn_stat.st_nlink++;
  np->dn_set_ctime = 1;
  
  diskfs_node_update (np, 1);

  error =  diskfs_direnter (dnp, name, np, ds, dircred);
  if (error)
    {
      np->dn_stat.st_nlink--;
      np->dn_set_ctime = 1;
    }

 out:
  mutex_unlock (&dnp->lock);
  mutex_unlock (&np->lock);
  return error;
}
