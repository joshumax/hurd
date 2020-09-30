/* Process st_?tim updates marked for a diskfs node.

   Copyright (C) 1994, 1996, 1999, 2000, 2007, 2009 Free Software Foundation,
   Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include <maptime.h>

/* If the disk is not readonly and noatime is not set, then check relatime
   conditions: if either `np->dn_stat.st_mtim.tv_sec' or
   `np->dn_stat.st_ctim.tv_sec' is greater than `np->dn_stat.st_atim.tv_sec',
   or if the atime is greater than 24 hours old, return true.
   */
int
atime_should_update (struct node *np)
{
  struct timeval t;

  if (_diskfs_noatime)
    return 0;

  if (_diskfs_relatime)
    {
      /* Update atime if mtime is younger than atime. */
      if (np->dn_stat.st_mtim.tv_sec > np->dn_stat.st_atim.tv_sec)
        return 1;
      /* Update atime if ctime is younger than atime. */
      if (np->dn_stat.st_ctim.tv_sec > np->dn_stat.st_atim.tv_sec)
        return 1;
      /* Update atime if current atime is more than 24 hours old. */
      maptime_read (diskfs_mtime, &t);
      if ((long)(t.tv_sec - np->dn_stat.st_atim.tv_sec) >= 24 * 60 * 60)
          return 1;
      return 0;
    }

  return 1; /* strictatime */
}

/* If disk is not readonly and the noatime option is not enabled, set
   NP->dn_set_atime.  If relatime is enabled, only set NP->dn_set_atime
   if the atime has not been updated today, or if ctime or mtime are
   more recent than atime */
void
diskfs_set_node_atime (struct node *np)
{
  if (!diskfs_check_readonly () && atime_should_update (np))
    np->dn_set_atime = 1;
}

/* If NP->dn_set_ctime is set, then modify NP->dn_stat.st_ctim
   appropriately; do the analogous operation for atime and mtime as well. */
void
diskfs_set_node_times (struct node *np)
{
  struct timeval t;

  if (!np->dn_set_mtime && !np->dn_set_atime && !np->dn_set_ctime)
    return;

  maptime_read (diskfs_mtime, &t);

  /* We are careful to test and reset each of these individually, so there
     is no race condition where a dn_set_?time flag setting gets lost.  It
     is not a problem to have the kind of race where the flag is set after
     we've tested it and done nothing--as long as the flag remains set so
     the update will happen at the next call.  */
  if (np->dn_set_mtime)
    {
      np->dn_stat.st_mtim.tv_sec = t.tv_sec;
      np->dn_stat.st_mtim.tv_nsec = t.tv_usec * 1000;
      np->dn_stat_dirty = 1;
      np->dn_set_mtime = 0;
    }
  if (np->dn_set_atime)
    {
      np->dn_stat.st_atim.tv_sec = t.tv_sec;
      np->dn_stat.st_atim.tv_nsec = t.tv_usec * 1000;
      np->dn_stat_dirty = 1;
      np->dn_set_atime = 0;
    }
  if (np->dn_set_ctime)
    {
      np->dn_stat.st_ctim.tv_sec = t.tv_sec;
      np->dn_stat.st_ctim.tv_nsec = t.tv_usec * 1000;
      np->dn_stat_dirty = 1;
      np->dn_set_ctime = 0;
    }
}
