/* Making new files
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

/* Create a new node and link it into DIR with as NAME.  Give it MODE;
   if that includes IFDIR, also initialize `.' and `..' in the new
   directory.  Return the node in NPP.  CRED identifies the user responsible
   for the call, and DS is the result of a prior diskfs_lookup for creation. */
error_t
diskfs_create_and_link (struct node *dir,
			char *name,
			mode_t mode,
			struct node **newnode,
			struct protid *cred,
			struct dirstat *ds)
{
  struct node *np;
  error_t err;
  int dirinc = 0;
  int number;
  
  /* Make the node */
  err = diskfs_alloc_node (dir, mode, &number);
  if (err)
    {
      diskfs_drop_dirstat (ds);
      return err;
    }
  *newnode = np = diskfs_nget (newnode);
  
  /* Initialize the on-disk fields. */
  
  if (cred->nuids)
    np->dn_stat.st_uid = cred->uids[0];
  else
    {
      np->dn_stat.st_uid = dir->dn_stat.st_uid;
      mode &= ~S_ISUID;
    }

  if (groupmember (dir->dn_stat.st_gid, cred))
    np->dn_stat.st_gid = dir->dn_stat.st_gid;
  else if (cred->ngids)
    np->dn_stat.st_gid = cred->gids[0];
  else
    {
      np->dn_stat.st_gid = dir->dn_stat.st_gid;
      mode &= ~S_ISGID;
    }
  
  np->dn_stat.st_rdev = 0;
  np->dn_stat.st_nlink = 1;
  np->dn_stat.st_mode = mode;

  np->dn_stat.st_blocks = 0;
  np->dn_stat.st_size = 0;
  np->dn_stat.st_flags = 0;
  np->dn_set_atime = 1;
  np->dn_set_mtime = 1;
  np->dn_set_ctime = 1;

  if (S_ISDIR (mode))
    err = diskfs_init_dir (np, dir);
  
  diskfs_node_update (np, 1);

  if (err)
    {
      diskfs_drop_dirstat (ds);
      return err;
    }
  
  err = diskfs_direnter (dir, name, np, ds, cred);
  if (err)
    {
      if (S_ISDIR (mode))
	diskfs_clear_directory (np);
      np->dn_stat.st_nlink = 0;
      np->dn_set_ctime = 1;
      diskfs_nput (np);
    }
  return err;
}
