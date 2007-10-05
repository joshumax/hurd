/* Process st_?time updates marked for a diskfs node.

   Copyright (C) 1994, 1996, 1999, 2000, 2007 Free Software Foundation, Inc.

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

/* If disk is not readonly and the noatime option is not enabled, set
   NP->dn_set_atime.  */
void
diskfs_set_node_atime (struct node *np)
{
  if (!_disk_noatime && !diskfs_check_readonly ())
    np->dn_set_atime = 1;
}

/* If NP->dn_set_ctime is set, then modify NP->dn_stat.st_ctime
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
#ifdef notyet
      np->dn_stat.st_mtimespec.ts_sec = t.tv_sec;
      np->dn_stat.st_mtimespec.ts_nsec = t.tv_usec * 1000;
#else
      np->dn_stat.st_mtime = t.tv_sec;
      np->dn_stat.st_mtime_usec = t.tv_usec;
#endif
      np->dn_stat_dirty = 1;
      np->dn_set_mtime = 0;
    }
  if (np->dn_set_atime)
    {
#ifdef notyet
      np->dn_stat.st_atimespec.ts_sec = t.tv_sec;
      np->dn_stat.st_atimespec.ts_nsec = t.tv_usec * 1000;
#else
      np->dn_stat.st_atime = t.tv_sec;
      np->dn_stat.st_atime_usec = t.tv_usec;
#endif
      np->dn_stat_dirty = 1;
      np->dn_set_atime = 0;
    }
  if (np->dn_set_ctime)
    {
#ifdef notyet
      np->dn_stat.st_ctimespec.ts_sec = t.tv_sec;
      np->dn_stat.st_ctimespec.ts_nsec = t.tv_usec * 1000;
#else
      np->dn_stat.st_ctime = t.tv_sec;
      np->dn_stat.st_ctime_usec = t.tv_usec;
#endif
      np->dn_stat_dirty = 1;
      np->dn_set_ctime = 0;
    }
}
