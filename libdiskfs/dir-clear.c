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

error_t
diskfs_clear_directory (struct node *dp,
			struct node *pdp,
			struct protid *cred)
{
  error_t err;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  
  /* Find and remove the `.' entry. */
  err = diskfs_lookup (dp, ".", REMOVE, 0, ds, cred);
  assert (err != ENOENT);
  if (!err)
    err = diskfs_dirremove (dp, ds);
  else
    diskfs_drop_dirstat (ds);
  if (err)
    return err;
  
  /* Decrement the link count */
  dp->dn_stat.st_nlink--;
  dp->dn_set_ctime = 1;

  /* Find and remove the `..' entry. */
  err = ufs_checkdirmod (dp, dp->dn_stat.st_mode, pdp, cred);
  if (!err)
    err = diskfs_lookup (dp, "..", REMOVE | SPEC_DOTDOT, 0, ds, cred);
  assert (err != ENOENT);
  if (!err)
    err = diskfs_dirremove (dp, ds);
  else
    diskfs_drop_dirstat (ds);
  if (err)
    return err;

  /* Decrement the link count on the parent */
  pdp->dn_stat.st_nlink--;
  pdp->dn_set_ctime = 1;

  return err;
}
