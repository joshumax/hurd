/* Wrapper for diskfs_dirrewrite_hard
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


#include "priv.h"

/* This will only be called after a successful call to diskfs_lookup
   of type RENAME; this call should change the name found in directory
   DP to point to node NP instead of its previous referent, OLDNP.  DP
   has been locked continuously since the call to diskfs_lookup and DS
   is as that call set it; NP is locked.  This routine should call
   diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  NAME is the
   name of OLDNP inside DP; it is this reference which is being
   rewritten. This function is a wrapper for diskfs_dirrewrite_hard.  */
error_t diskfs_dirrewrite (struct node *dp,
			   struct node *oldnp,
			   struct node *np,
			   const char *name,
			   struct dirstat *ds)
{
  error_t err;

  diskfs_purge_lookup_cache (dp, oldnp);

  err = diskfs_dirrewrite_hard (dp, np, ds);
  if (err)
    return err;

  if (dp->dirmod_reqs)
    diskfs_notice_dirchange (dp, DIR_CHANGED_RENUMBER, name);
  diskfs_enter_lookup_cache (dp, np, name);
  return 0;
}
