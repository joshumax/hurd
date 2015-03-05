/*
   Copyright (C) 1993, 1994 Free Software Foundation

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

/* Set on disk fields from NP->dn_stat; update ctime, atime, and mtime
   if necessary.  If WAIT is true, then return only after the physical
   media has been completely updated.  */
void diskfs_node_update (struct node *np, int wait)
{
  diskfs_set_node_times (np);
  if (np->dn_stat_dirty)
    diskfs_write_disknode (np, wait);
}
