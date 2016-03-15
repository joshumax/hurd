/* 
   Copyright (C) 1999, 2000 Free Software Foundation, Inc.
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

void
netfs_nput (struct node *np)
{
  struct references result;

  refcounts_demote (&np->refcounts, &result);

  if (result.hard == 0)
    netfs_try_dropping_softrefs (np);

  refcounts_deref_weak (&np->refcounts, &result);

  if (result.hard == 0 && result.weak == 0)
    netfs_drop_node (np);
  else
    pthread_mutex_unlock (&np->lock);
}

/* The last hard reference to NP has gone away; the user must define
   this function in order to drop all the soft references.  */
void __attribute__ ((weak))
netfs_try_dropping_softrefs (struct node *np)
{
}
