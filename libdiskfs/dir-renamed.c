/* 
   Copyright (C) 1994 Free Software Foundation

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


/* Rename directory node FNP (whose parent is FDP, and which has name
   FROMNAME in that directory) to have name TONAME inside directory
   TDP.  None of these nodes are locked, and none should be locked
   upon return.  This routine is serialized, so it doesn't have to be
   reentrant.  Directories will never be renamed except by this
   routine.  FROMCRED and TOCRED are the users responsible for
   FDP/FNP and TDP respectively.  */
error_t
diskfs_rename_dir (struct node *fdp, struct node *fnp, char *fromname,
		   struct node *tdp, char *toname, struct protid *fromcred,
		   struct protid *tocred)
{
  error_t err;
  struct node *tnp, *tmpnp;
  void *buf = alloca (diskfs_dirstat_size);
  struct dirstat *ds;
  struct dirstat *tmpds;

  mutex_lock (&tdp->lock);
  diskfs_nref (tdp);		/* reference and lock will get consumed by
				   checkpath */
  err = diskfs_checkpath (fnp, tdp, tocred);
  
  if (err)
    return err;
  
  /* Now, lock the parent directories.  This is legal because tdp is not
     a child of fnp (guaranteed by checkpath above). */
  mutex_lock (&fdp->lock);
  if (fdp != tdp)
    mutex_lock (&tdp->lock);
  
  /* 1: Lookup target; if it exists, make sure it's an empty directory. */
  mutex_lock (&tdp->lock);
  ds = buf;
  err = diskfs_lookup (tdp, toname, RENAME, &tnp, ds, tocred);
  
  if (tnp == fnp)
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nrele (tnp);
      mutex_unlock (&tdp->lock);
      mutex_unlock (&fdp->lock);
      return 0;
    }
  
  /* Now we can safely lock fnp */
  mutex_lock (&fnp->lock);

  if (tnp)
    {
      if (! S_ISDIR(tnp->dn_stat.st_mode))
	err = ENOTDIR;
      else if (!diskfs_dirempty (tnp, tocred))
	err = ENOTEMPTY;
    }     

  if (err && err != ENOENT)
    goto out;

  /* 2: Set our .. to point to the new parent */
  if (tdp->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      err = EMLINK;
      return EMLINK;
    }
  tdp->dn_stat.st_nlink++;
  tdp->dn_set_ctime = 1;
  err = diskfs_checkdirmod (fnp, fdp, fromcred);
  if (err)
    goto out;
  
  tmpds = alloca (diskfs_dirstat_size);
  err = diskfs_lookup (fnp, "..", RENAME | SPEC_DOTDOT, 
		       &tmpnp, tmpds, fromcred);
  assert (err != ENOENT);
  assert (tmpnp == fdp);
  diskfs_nrele (tmpnp);
  if (err)
    {
      diskfs_drop_dirstat (fnp, tmpds);
      goto out;
    }

  err = diskfs_dirrewrite (fnp, tdp, tmpds);
  if (err)
    goto out;
  
  fdp->dn_stat.st_nlink--;
  fdp->dn_set_ctime = 1;


  /* 3: Increment the link count on the node being moved and rewrite
     tdp. */
  if (fnp->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      mutex_unlock (&fnp->lock);
      diskfs_drop_dirstat (tdp, ds);
      mutex_unlock (&tdp->lock);
      if (tnp)
	diskfs_nput (tnp);
      return EMLINK;
    }
  fnp->dn_stat.st_nlink++;
  fnp->dn_set_ctime = 1;
  diskfs_node_update (fnp, 1);
  
  if (tnp)
    {
      err = diskfs_dirrewrite (tdp, fnp, ds);
      ds = 0;
      if (!err)
	{
	  tnp->dn_stat.st_nlink--;
	  tnp->dn_set_ctime = 1;
	}
      diskfs_clear_directory (tnp, tdp, tocred);
    }
  else
    err = diskfs_direnter (tdp, toname, fnp, ds, tocred);

  if (err)
    goto out;

  /* 4: Remove the entry in fdp. */
  ds = buf;
  mutex_unlock (&fnp->lock);
  err = diskfs_lookup (fdp, fromname, REMOVE, &tmpnp, ds, fromcred);
  assert (tmpnp == fnp);
  if (err)
    goto out;
  
  diskfs_dirremove (fdp, ds);
  ds = 0;
  fnp->dn_stat.st_nlink--;
  fnp->dn_set_ctime = 1;
  
 out:
  if (tdp)
    mutex_unlock (&tdp->lock);
  if (tnp)
    diskfs_nput (tnp);
  if (fdp)
    mutex_unlock (&fdp->lock);
  if (fnp)
    mutex_unlock (&fnp->lock);
  if (ds)
    diskfs_drop_dirstat (tdp, ds);
  return err;
}
