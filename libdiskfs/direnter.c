/* Wrapper for diskfs_direnter_hard
   Copyright (C) 1996, 1998 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */


#include <errno.h>

#include "diskfs.h"

/* Add NP to directory DP under the name NAME.  This will only be
   called after an unsuccessful call to diskfs_lookup of type CREATE
   or RENAME; DP has been locked continuously since that call and DS
   is as that call set it, NP is locked.  CRED identifies the user
   responsible for the call (to be used only to validate directory
   growth).  This function is a wrapper for diskfs_direnter_hard.  */
error_t
diskfs_direnter (struct node *dp,
		 const char *name,
		 struct node *np,
		 struct dirstat *ds,
		 struct protid *cred)
{
  error_t err;

  /* A directory that was removed, by rmdir or by a rename onto it, stays
     reachable through the ports and working directories still on it, but
     nothing reaches what is entered in it.  */
  if (dp->dn_stat.st_nlink == 0)
    {
      diskfs_drop_dirstat (dp, ds);
      return ENOENT;
    }

  err = diskfs_direnter_hard (dp, name, np, ds, cred);
  if (err)
    return err;

  if (dp->dirmod_reqs)
    diskfs_notice_dirchange (dp, DIR_CHANGED_NEW, name);

  diskfs_enter_lookup_cache (dp, np, name);
  return 0;
}
