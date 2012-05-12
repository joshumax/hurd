/* libdiskfs implementation of fs.defs: dir_link
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

/* Implement dir_link as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_link (struct protid *dircred,
		   struct protid *filecred,
		   char *name,
		   int excl)
{
  struct node *np;		/* node being linked */
  struct node *tnp;		/* node being deleted implicitly */
  struct node *dnp;		/* directory of new entry */
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  error_t error;

  if (!dircred)
    return EOPNOTSUPP;

  if (diskfs_check_readonly ())
    return EROFS;

  if (!filecred)
    return EXDEV;

  np = filecred->po->np;
  mutex_lock (&np->lock);
  if (S_ISDIR (np->dn_stat.st_mode))
    {
      mutex_unlock (&np->lock);
      return EPERM;
    }
  mutex_unlock (&np->lock);

  dnp = dircred->po->np;
  mutex_lock (&dnp->lock);

  /* Lookup new location */
  error = diskfs_lookup (dnp, name, RENAME, &tnp, ds, dircred);
  if (!error && excl)
    {
      error = EEXIST;
      diskfs_nput (tnp);
    }
  if (error && error != ENOENT)
    {
      if (error == EAGAIN)
	error = EINVAL;
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      return error;
    }

  if (np == tnp)
    {
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      mutex_unlock (&tnp->lock);
      mach_port_deallocate (mach_task_self (), filecred->pi.port_right);
      return 0;
    }

  if (tnp && S_ISDIR (tnp->dn_stat.st_mode))
    {
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      mutex_unlock (&tnp->lock);
      return EISDIR;
    }

  /* Create new entry for NP */

  /* This is safe because NP is not a directory (thus not DNP) and
     not TNP and is a leaf. */
  mutex_lock (&np->lock);

  /* Increment link count */
  if (np->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&np->lock);
      mutex_unlock (&dnp->lock);
      return EMLINK;
    }
  np->dn_stat.st_nlink++;
  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  /* Attach it */
  if (tnp)
    {
      assert (!excl);
      error = diskfs_dirrewrite (dnp, tnp, np, name, ds);
      if (!error)
	{
	  /* Deallocate link on TNP */
	  tnp->dn_stat.st_nlink--;
	  tnp->dn_set_ctime = 1;
	  if (diskfs_synchronous)
	    diskfs_node_update (tnp, 1);
	}
      diskfs_nput (tnp);
    }
  else
    error = diskfs_direnter (dnp, name, np, ds, dircred);

  if (diskfs_synchronous)
    diskfs_node_update (dnp, 1);

  mutex_unlock (&dnp->lock);
  mutex_unlock (&np->lock);
  if (!error)
    /* MiG won't do this for us, which it ought to. */
    mach_port_deallocate (mach_task_self (), filecred->pi.port_right);
  return error;
}
