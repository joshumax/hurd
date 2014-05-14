/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

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

/* Add a hard reference to a node.  If there were no hard
   references previously, then the node cannot be locked
   (because you must hold a hard reference to hold the lock). */
void
diskfs_nref (struct node *np)
{
  struct references result;
  refcounts_ref (&np->refcounts, &result);
  if (result.hard == 1)
    {
      pthread_mutex_lock (&np->lock);
      diskfs_new_hardrefs (np);
      pthread_mutex_unlock (&np->lock);
    }
}
