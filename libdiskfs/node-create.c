/* Making new files
   Copyright (C) 1992, 1993, 1994, 1996, 1998 Free Software Foundation

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

/* Create a new node. Give it MODE; if that includes IFDIR, also
   initialize `.' and `..' in the new directory.  Return the node in NPP.
   CRED identifies the user responsible for the call.  If NAME is nonzero,
   then link the new node into DIR with name NAME; DS is the result of a
   prior diskfs_lookup for creation (and DIR has been held locked since).
   DIR must always be provided as at least a hint for disk allocation
   strategies.  */
error_t
diskfs_create_node (struct node *dir,
		    const char *name,
		    mode_t mode,
		    struct node **newnode,
		    struct protid *cred,
		    struct dirstat *ds)
{
  struct node *np;
  error_t err;
  uid_t newuid;
  gid_t newgid;

  if (diskfs_check_readonly ())
    return EROFS;

  /* Make the node */
  err = diskfs_alloc_node (dir, mode, newnode);
  if (err)
    {
      if (name)
	diskfs_drop_dirstat (dir, ds);
      return err;
    }

  np = *newnode;

  /* Initialize the on-disk fields. */
  if (cred->user->uids->num)
    newuid = cred->user->uids->ids[0];
  else
    {
      newuid = dir->dn_stat.st_uid;
      mode &= ~S_ISUID;
    }
  err = diskfs_validate_owner_change (np, newuid);
  if (err)
    goto change_err;
  np->dn_stat.st_uid = newuid;
  if (np->author_tracks_uid)
    np->dn_stat.st_author = newuid;

  newgid = dir->dn_stat.st_gid;
  if (!idvec_contains (cred->user->gids, newgid))
    mode &= ~S_ISGID;
  err = diskfs_validate_group_change (np, newgid);
  if (err)
    goto change_err;
  np->dn_stat.st_gid = newgid;

  np->dn_stat.st_rdev = 0;
  np->dn_stat.st_nlink = !!name;
  err = diskfs_validate_mode_change (np, mode);
  if (err)
    goto change_err;
  np->dn_stat.st_mode = mode;

  np->dn_stat.st_blocks = 0;
  np->dn_stat.st_size = 0;
  np->dn_stat.st_flags = 0;
  np->dn_set_atime = 1;
  np->dn_set_mtime = 1;
  np->dn_set_ctime = 1;

  if (S_ISDIR (mode))
    err = diskfs_init_dir (np, dir, cred);

  diskfs_node_update (np, 1);

  if (err)
    {
    change_err:
      np->dn_stat.st_mode = 0;
      np->dn_stat.st_nlink = 0;
      if (name)
	diskfs_drop_dirstat (dir, ds);
      return err;
    }

  if (name)
    {
      err = diskfs_direnter (dir, name, np, ds, cred);
      if (err)
	{
	  if (S_ISDIR (mode))
	    diskfs_clear_directory (np, dir, cred);
	  np->dn_stat.st_nlink = 0;
	  np->dn_set_ctime = 1;
	  diskfs_nput (np);
	}
    }
  return err;
}
