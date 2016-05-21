/* libdsikfs implementation of fs.defs: dir_rmdir
   Copyright (C) 1992,93,94,95,96,97,99 Free Software Foundation, Inc.

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
#include <hurd/fsys.h>

/* Implement dir_rmdir as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_rmdir (struct protid *dircred,
		    char *name)
{
  struct node *dnp;
  struct node *np = NULL;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  error_t error;

  /* This routine cleans up the state we have after calling diskfs_lookup.
     After that call, all returns are done with `return done (ERROR, NP);'.  */
  inline error_t done (error_t error, struct node *np)
    {
      if (np)
	diskfs_nput (np);

      if (ds)
	diskfs_drop_dirstat (dnp, ds);
      pthread_mutex_unlock (&dnp->lock);

      return error;
    }

  if (!dircred)
    return EOPNOTSUPP;

  dnp = dircred->po->np;
  if (diskfs_check_readonly ())
    return EROFS;

  pthread_mutex_lock (&dnp->lock);

  error = diskfs_lookup (dnp, name, REMOVE, &np, ds, dircred);
  if (error)
    return done (error == EAGAIN ? ENOTEMPTY : error, 0);

  if (dnp == np)
    {
      /* Attempt to rmdir(".") */
      diskfs_nrele (np);
      diskfs_drop_dirstat (dnp, ds);
      pthread_mutex_unlock (&dnp->lock);
      return EINVAL;
    }

  if ((np->dn_stat.st_mode & S_IPTRANS) || fshelp_translated (&np->transbox))
    /* Attempt to rmdir a translated node.  */
    return done (EBUSY, np);

  if (!S_ISDIR (np->dn_stat.st_mode))
    return done (ENOTDIR, np);

  if (!diskfs_dirempty (np, dircred))
    return done (ENOTEMPTY, np);

  /* Here we go!  */
  error = diskfs_dirremove (dnp, np, name, ds);
  ds = 0;

  if (!error)
    {
      np->dn_stat.st_nlink--;
      np->dn_set_ctime = 1;
      diskfs_clear_directory (np, dnp, dircred);
      if (diskfs_synchronous)
	diskfs_file_update (np, 1);
    }
  if (diskfs_synchronous)
    diskfs_file_update (dnp, 1);

  return done (error, np);
}
