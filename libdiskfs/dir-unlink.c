/* libdiskfs implementation of fs.defs: dir_unlink
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
#include <hurd/fsys.h>

/* Implement dir_unlink as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_unlink (struct protid *dircred,
		     char *name)
{
  struct node *dnp;
  struct node *np;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  error_t err;
  mach_port_t control = MACH_PORT_NULL;

  if (!dircred)
    return EOPNOTSUPP;

  dnp = dircred->po->np;
  if (diskfs_check_readonly ())
    return EROFS;

  pthread_mutex_lock (&dnp->lock);

  err = diskfs_lookup (dnp, name, REMOVE, &np, ds, dircred);
  if (err == EAGAIN)
    err = EPERM;	/* 1003.1-1996 5.5.1.4 */
  if (err)
    {
      diskfs_drop_dirstat (dnp, ds);
      pthread_mutex_unlock (&dnp->lock);
      return err;
    }

  /* This isn't the BSD behavior, but it is Posix compliant and saves
     us on several race conditions.*/
  if (S_ISDIR(np->dn_stat.st_mode))
    {
      if (np == dnp)		/* gotta catch '.' */
	diskfs_nrele (np);
      else
	diskfs_nput (np);
      diskfs_drop_dirstat (dnp, ds);
      pthread_mutex_unlock (&dnp->lock);
      return EPERM;		/* 1003.1-1996 5.5.1.4 */
    }

  err = diskfs_dirremove (dnp, np, name, ds);
  if (diskfs_synchronous)
    diskfs_node_update (dnp, 1);
  if (err)
    {
      diskfs_nput (np);
      pthread_mutex_unlock (&dnp->lock);
      return err;
    }

  np->dn_stat.st_nlink--;
  np->dn_set_ctime = 1;
  if (diskfs_synchronous)
    diskfs_node_update (np, 1);

  if (np->dn_stat.st_nlink == 0)
    fshelp_fetch_control (&np->transbox, &control);

  /* This check is necessary because we might get here on an error while
     checking the mode on something which happens to be `.'. */
  if (np == dnp)
    diskfs_nrele (np);
  else
    diskfs_nput (np);
  pthread_mutex_unlock (&dnp->lock);

  if (control)
    {
      fsys_goaway (control, FSYS_GOAWAY_UNLINK);
      mach_port_deallocate (mach_task_self (), control);
    }

  return err;
}
