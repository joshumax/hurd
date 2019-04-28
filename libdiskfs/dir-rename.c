/* libdiskfs implementation of fs.defs: dir_rename

   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 2007
     Free Software Foundation

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
#include <string.h>

/* To avoid races in checkpath, and to prevent a directory from being
   simultaneously renamed by two processes, we serialize all renames of
   directores with this lock */
static pthread_mutex_t renamedirlock = PTHREAD_MUTEX_INITIALIZER;

/* Implement dir_rename as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_rename (struct protid *fromcred,
		     char *fromname,
		     struct protid *tocred,
		     char *toname,
		     int excl)
{
  struct node *fdp, *tdp, *fnp, *tnp, *tmpnp;
  error_t err;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  
  if (!fromcred)
    return EOPNOTSUPP;

  /* Verify that tocred really is a port to us. */
  if (! tocred)
    return EXDEV;

  if (!strcmp (fromname, ".") || !strcmp (fromname, "..")
   || !strcmp (toname,   ".") || !strcmp (toname,   ".."))
    return EINVAL;

  if (tocred->po->shadow_root != fromcred->po->shadow_root)
    /* Same translator, but in different shadow trees.  */
    return EXDEV;

  if (diskfs_check_readonly ())
    return EROFS;

  fdp = fromcred->po->np;
  tdp = tocred->po->np;

 try_again:
  /* Acquire the source; hold a reference to it.  This 
     will prevent anyone from deleting it before we create
     the new link. */
  pthread_mutex_lock (&fdp->lock);
  err = diskfs_lookup (fdp, fromname, LOOKUP, &fnp, 0, fromcred);
  pthread_mutex_unlock (&fdp->lock);
  if (err == EAGAIN)
    err = EINVAL;
  if (err)
    return err;

  if (S_ISDIR (fnp->dn_stat.st_mode))
    {
      pthread_mutex_unlock (&fnp->lock);
      if (pthread_mutex_trylock (&renamedirlock))
	{
	  diskfs_nrele (fnp);
	  goto try_again;
	}
      err = diskfs_rename_dir (fdp, fnp, fromname, tdp, toname, fromcred,
			       tocred, excl);
      if (diskfs_synchronous)
	{
	  pthread_mutex_lock (&fdp->lock);
	  diskfs_file_update (fdp, 1);
	  pthread_mutex_unlock (&fdp->lock);
	  
	  pthread_mutex_lock (&fnp->lock);
	  diskfs_file_update (fnp, 1);
	  pthread_mutex_unlock (&fnp->lock);

	  pthread_mutex_lock (&tdp->lock);
	  diskfs_file_update (tdp, 1);
	  pthread_mutex_unlock (&tdp->lock);
	}
      
      diskfs_nrele (fnp);
      pthread_mutex_unlock (&renamedirlock);
      if (!err)
	/* MiG won't do this for us, which it ought to. */
	mach_port_deallocate (mach_task_self (), tocred->pi.port_right);
      return err;
    }

  pthread_mutex_unlock (&fnp->lock);

  /* We now hold no locks */

  /* Link the node into the new directory. */
  pthread_mutex_lock (&tdp->lock);
  
  err = diskfs_lookup (tdp, toname, RENAME, &tnp, ds, tocred);
  if (err == EAGAIN)
    err = EINVAL;
  else if (!err && excl)
    {
      err = EEXIST;
      diskfs_nput (tnp);
    }
  if (err && err != ENOENT)
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nrele (fnp);
      pthread_mutex_unlock (&tdp->lock);
      return err;
    }

  /* rename("foo", "link-to-foo") is guaranteed to return 0 and
     do nothing by Posix. */
  if (tnp == fnp)
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nrele (fnp);
      diskfs_nput (tnp);
      pthread_mutex_unlock (&tdp->lock);
      mach_port_deallocate (mach_task_self (), tocred->pi.port_right);
      return 0;
    }

  /* rename("foo", dir) should fail. */
  if (tnp && S_ISDIR (tnp->dn_stat.st_mode))
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nrele (fnp);
      diskfs_nput (tnp);
      pthread_mutex_unlock (&tdp->lock);
      return EISDIR;
    }

  pthread_mutex_lock (&fnp->lock);

  /* Increment the link count for the upcoming link */
  if (fnp->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nput (fnp);
      if (tnp)
        diskfs_nput (tnp);
      pthread_mutex_unlock (&tdp->lock);
      return EMLINK;
    }
  fnp->dn_stat.st_nlink++;
  fnp->dn_set_ctime = 1;
  diskfs_node_update (fnp, diskfs_synchronous);

  if (tnp)
    {
      err = diskfs_dirrewrite (tdp, tnp, fnp, toname, ds);
      if (!err)
	{
	  tnp->dn_stat.st_nlink--;
	  tnp->dn_set_ctime = 1;
	  if (diskfs_synchronous)
	    diskfs_node_update (tnp, 1);
	}
      diskfs_nput (tnp);
    }
  else
    err = diskfs_direnter (tdp, toname, fnp, ds, tocred);

  if (diskfs_synchronous)
    diskfs_node_update (tdp, 1);

  pthread_mutex_unlock (&tdp->lock);
  pthread_mutex_unlock (&fnp->lock);
  if (err)
    {
      diskfs_nrele (fnp);
      return err;
    }

  /* We now hold no locks */

  /* Now we remove the source.  Unfortunately, we haven't held 
     fdp locked (nor could we), so someone else might have already
     removed it. */
  pthread_mutex_lock (&fdp->lock);
  err = diskfs_lookup (fdp, fromname, REMOVE, &tmpnp, ds, fromcred);
  if (err)
    {
      diskfs_drop_dirstat (tdp, ds);
      pthread_mutex_unlock (&fdp->lock);
      diskfs_nrele (fnp);
      return err;
    }

  if (tmpnp != fnp)
    {
      /* This is no longer the node being renamed, so just return. */
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nput (tmpnp);
      diskfs_nrele (fnp);
      pthread_mutex_unlock (&fdp->lock);
      mach_port_deallocate (mach_task_self (), tocred->pi.port_right);
      return 0;
    }
  
  diskfs_nrele (tmpnp);

  err = diskfs_dirremove (fdp, fnp, fromname, ds);
  if (diskfs_synchronous)
    diskfs_node_update (fdp, 1);

  fnp->dn_stat.st_nlink--;
  fnp->dn_set_ctime = 1;
  
  if (diskfs_synchronous)
    diskfs_node_update (fnp, 1);
  
  diskfs_nput (fnp);
  pthread_mutex_unlock (&fdp->lock);
  if (!err)
    mach_port_deallocate (mach_task_self (), tocred->pi.port_right);

  return err;
}
