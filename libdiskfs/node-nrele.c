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

/* Release a hard reference on NP.  If NP is locked by anyone, then
   this cannot be the last hard reference (because you must hold a
   hard reference in order to hold the lock).  If this is the last
   hard reference and there are no links, then request soft references
   to be dropped.  */
void
diskfs_nrele (struct node *np)
{
  int locked = FALSE;
  struct references result;

  /* While we call the diskfs_try_dropping_softrefs, we need to hold
     one reference.  We use a weak reference for this purpose, which
     we acquire by demoting our hard reference to a weak one.  */
  refcounts_demote (&np->refcounts, &result);

  if (result.hard == 0)
    {
      locked = TRUE;
      pthread_mutex_lock (&np->lock);
      _diskfs_lastref (np);
    }

  /* Finally get rid of our reference.  */
  refcounts_deref_weak (&np->refcounts, &result);

  if (result.hard == 0 && result.weak == 0)
    {
      if (! locked)
        pthread_mutex_lock (&np->lock);
      diskfs_drop_node (np);
    }
  else if (locked)
    pthread_mutex_unlock (&np->lock);
}
