/* Wrapper for diskfs_dirremove_hard
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
   of type REMOVE; this call should remove the name found from the
   directory DS.  DP has been locked continuously since the call to
   diskfs_lookup and DS is as that call set it.  This routine should
   call diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  This
   function is a wrapper for diskfs_dirremove_hard.  The entry being
   removed has name NAME and refers to NP.  */
error_t
diskfs_dirremove (struct node *dp,
		  struct node *np,
		  const char *name,
		  struct dirstat *ds)
{
  error_t err;

  diskfs_purge_lookup_cache (dp, np);

  err = diskfs_dirremove_hard (dp, ds);

  if (!err && dp->dirmod_reqs)
    diskfs_notice_dirchange (dp, DIR_CHANGED_UNLINK, name);
  return err;
}
