/* 
   Copyright (C) 1994, 1995, 1996, 1997 Free Software Foundation

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

/* Locked node DP is a new directory; add whatever links are necessary
   to give it structure; its parent is the (locked) node PDP. 
   This routine may not call diskfs_lookup on PDP.  The new directory
   must be clear within the meaning of diskfs_dirempty. 
   CRED identifies the user making the call.  */
error_t
diskfs_init_dir (struct node *dp, struct node *pdp, struct protid *cred)
{
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  struct node *foo;
  error_t err;

  /* Fabricate a protid that represents root credentials. */
  static uid_t zero = 0;
  static struct idvec vec = {&zero, 1, 1};
  static struct iouser user = {&vec, &vec, 0};
  struct protid lookupcred = {{ .refcounts = { .references = {1, 0}}},
			      &user, cred->po, 0, 0};

  /* New links */
  if (pdp->dn_stat.st_nlink == diskfs_link_max - 1)
    return EMLINK;

  dp->dn_stat.st_nlink++;	/* for `.' */
  dp->dn_set_ctime = 1;
  err = diskfs_lookup (dp, ".", CREATE, &foo, ds, &lookupcred);
  assert_backtrace (err == ENOENT);
  err = diskfs_direnter (dp, ".", dp, ds, cred);
  if (err)
    {
      dp->dn_stat.st_nlink--;
      dp->dn_set_ctime = 1;
      return err;
    }

  pdp->dn_stat.st_nlink++;	/* for `..' */
  pdp->dn_set_ctime = 1;
  err = diskfs_lookup (dp, "..", CREATE, &foo, ds, &lookupcred);
  assert_backtrace (err == ENOENT);
  err = diskfs_direnter (dp, "..", pdp, ds, cred);
  if (err)
    {
      pdp->dn_stat.st_nlink--;
      pdp->dn_set_ctime = 1;
      return err;
    }

  diskfs_node_update (dp, diskfs_synchronous);
  return 0;
}
