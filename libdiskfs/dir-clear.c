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

/* Clear the `.' and `..' entries from directory DP.  Its parent is PDP,
   and the user responsible for this is identified by CRED.  Both 
   directories must be locked.  */
error_t
diskfs_clear_directory (struct node *dp,
			struct node *pdp,
			struct protid *cred)
{
  error_t err;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  struct node *np;

  /* Find and remove the `.' entry. */
  err = diskfs_lookup (dp, ".", REMOVE, &np, ds, cred);
  assert_backtrace (err != ENOENT);
  if (!err)
    {
      assert_backtrace (np == dp);
      err = diskfs_dirremove (dp, np, ".", ds);
      diskfs_nrele (np);
    }
  else
    diskfs_drop_dirstat (dp, ds);
  if (err)
    return err;
  
  /* Decrement the link count */
  dp->dn_stat.st_nlink--;
  dp->dn_set_ctime = 1;

  /* Find and remove the `..' entry. */
  err = diskfs_lookup (dp, "..", REMOVE | SPEC_DOTDOT, &np, ds, cred);
  assert_backtrace (err != ENOENT);
  if (!err)
    {
      assert_backtrace (np == pdp);
      err = diskfs_dirremove (dp, np, "..", ds);
    }
  else
    diskfs_drop_dirstat (dp, ds);
  if (err)
    return err;

  /* Decrement the link count on the parent */
  pdp->dn_stat.st_nlink--;
  pdp->dn_set_ctime = 1;

  diskfs_truncate (dp, 0);

  return err;
}
