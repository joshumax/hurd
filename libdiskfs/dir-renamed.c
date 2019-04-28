/*
   Copyright (C) 1994,95,96,97,98,99,2001,2003 Free Software Foundation, Inc.

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


/* Check if source directory is in the path of the target directory.
   We get target locked, source unlocked but with a reference.  When
   we return, nothing is locked, and target has lost its reference.
   This routine assumes that no renames of directories will happen
   while it is running; as a result, ufs_rename serializes all renames
   of directories.  */
static error_t
checkpath(struct node *source,
	  struct node *target,
	  struct protid *cred)
{
  error_t err;
  struct node *np;

  for (np = target, err = 0;
       /* nothing */;
       /* This special lookup does a diskfs_nput on its first argument
	  when it succeeds. */
       err = diskfs_lookup (np, "..", LOOKUP | SPEC_DOTDOT, &np, 0, cred))
    {
      if (err)
	{
	  diskfs_nput (np);
	  return err;
	}

      if (np == source)
	{
	  diskfs_nput (np);
	  return EINVAL;
	}

      if (np == diskfs_root_node || np == cred->po->shadow_root)
	{
	  diskfs_nput (np);
	  return 0;
	}
    }
}

/* Rename directory node FNP (whose parent is FDP, and which has name
   FROMNAME in that directory) to have name TONAME inside directory
   TDP.  None of these nodes are locked, and none should be locked
   upon return.  This routine is serialized, so it doesn't have to be
   reentrant.  Directories will never be renamed except by this
   routine.  FROMCRED and TOCRED are the users responsible for
   FDP/FNP and TDP respectively.  If EXCL is set, then fail if TONAME
   already exists inside directory TDP. */
error_t
diskfs_rename_dir (struct node *fdp, struct node *fnp, const char *fromname,
		   struct node *tdp, const char *toname,
		   struct protid *fromcred, struct protid *tocred, int excl)
{
  error_t err;
  struct node *tnp, *tmpnp;
  void *buf = alloca (diskfs_dirstat_size);
  struct dirstat *ds;
  struct dirstat *tmpds;

  pthread_mutex_lock (&tdp->lock);
  diskfs_nref (tdp);		/* reference and lock will get consumed by
				   checkpath */
  err = checkpath (fnp, tdp, tocred);

  if (err)
    return err;

  /* Now, lock the parent directories.  This is legal because tdp is not
     a child of fnp (guaranteed by checkpath above). */
  pthread_mutex_lock (&fdp->lock);
  if (fdp != tdp)
    pthread_mutex_lock (&tdp->lock);

  /* 1: Lookup target; if it exists, make sure it's an empty directory. */
  ds = buf;
  err = diskfs_lookup (tdp, toname, RENAME, &tnp, ds, tocred);
  assert_backtrace (err != EAGAIN);	/* <-> assert_backtrace (TONAME != "..") */
  if (err && err != ENOENT)
    goto out;
  if (tnp && excl)
    {
      err = EEXIST;
      goto out;
    }

  if (tnp == fnp)
    {
      diskfs_drop_dirstat (tdp, ds);
      diskfs_nput (tnp);
      pthread_mutex_unlock (&tdp->lock);
      if (fdp != tdp)
	pthread_mutex_unlock (&fdp->lock);
      return 0;
    }

  /* Check permissions to remove FROMNAME and lock FNP.  */
  tmpds = alloca (diskfs_dirstat_size);
  err = diskfs_lookup (fdp, fromname, REMOVE, &tmpnp, tmpds, fromcred);
  assert_backtrace (!tmpnp || tmpnp == fnp);
  if (tmpnp)
    diskfs_nrele (tmpnp);
  diskfs_drop_dirstat (fdp, tmpds);
  if (err)
    goto out;

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
  if (fdp != tdp)
    {
      if (tdp->dn_stat.st_nlink == diskfs_link_max - 1)
	{
	  err = EMLINK;
	  goto out;
	}
      tdp->dn_stat.st_nlink++;
      tdp->dn_set_ctime = 1;
      if (diskfs_synchronous)
	diskfs_node_update (tdp, 1);

      tmpds = alloca (diskfs_dirstat_size);
      err = diskfs_lookup (fnp, "..", RENAME | SPEC_DOTDOT,
			   &tmpnp, tmpds, fromcred);
      assert_backtrace (err != ENOENT);
      if (err)
	{
	  diskfs_drop_dirstat (fnp, tmpds);
	  goto out;
	}
      assert_backtrace (tmpnp == fdp);

      err = diskfs_dirrewrite (fnp, fdp, tdp, "..", tmpds);
      if (diskfs_synchronous)
	diskfs_file_update (fnp, 1);
      if (err)
	goto out;

      fdp->dn_stat.st_nlink--;
      fdp->dn_set_ctime = 1;
      if (diskfs_synchronous)
	diskfs_node_update (fdp, 1);
    }


  /* 3: Increment the link count on the node being moved and rewrite
     tdp. */
  if (fnp->dn_stat.st_nlink == diskfs_link_max - 1)
    {
      pthread_mutex_unlock (&fnp->lock);
      diskfs_drop_dirstat (tdp, ds);
      pthread_mutex_unlock (&tdp->lock);
      if (tnp)
	diskfs_nput (tnp);
      return EMLINK;
    }
  fnp->dn_stat.st_nlink++;
  fnp->dn_set_ctime = 1;
  diskfs_node_update (fnp, diskfs_synchronous);

  if (tnp)
    {
      err = diskfs_dirrewrite (tdp, tnp, fnp, toname, ds);
      ds = 0;
      if (!err)
	{
	  tnp->dn_stat.st_nlink--;
	  tnp->dn_set_ctime = 1;
	}
      diskfs_clear_directory (tnp, tdp, tocred);
      if (diskfs_synchronous)
	diskfs_file_update (tnp, 1);
    }
  else
    {
      err = diskfs_direnter (tdp, toname, fnp, ds, tocred);
      if (diskfs_synchronous)
	diskfs_file_update (tdp, 1);
    }

  if (err)
    goto out;

  /* 4: Remove the entry in fdp. */
  ds = buf;
  pthread_mutex_unlock (&fnp->lock);
  err = diskfs_lookup (fdp, fromname, REMOVE, &tmpnp, ds, fromcred);
  assert_backtrace (!tmpnp || tmpnp == fnp);
  if (tmpnp)
    diskfs_nrele (tmpnp);
  if (err)
    {
      assert_backtrace (!tmpnp);
      /* diskfs_lookup has not locked fnp then, do not unlock it. */
      fnp = NULL;
      goto out;
    }

  diskfs_dirremove (fdp, fnp, fromname, ds);
  ds = 0;
  fnp->dn_stat.st_nlink--;
  fnp->dn_set_ctime = 1;
  if (diskfs_synchronous)
    {
      diskfs_file_update (fdp, 1);
      diskfs_node_update (fnp, 1);
    }

 out:
  if (tdp)
    pthread_mutex_unlock (&tdp->lock);
  if (tnp)
    diskfs_nput (tnp);
  if (fdp && fdp != tdp)
    pthread_mutex_unlock (&fdp->lock);
  if (fnp)
    pthread_mutex_unlock (&fnp->lock);
  if (ds)
    diskfs_drop_dirstat (tdp, ds);
  return err;
}
