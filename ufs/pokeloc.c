/* Remember where we've written the disk to speed up sync
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ufs.h"

struct pokeloc 
{
  vm_offset_t offset;
  vm_size_t length;
  struct pokeloc *next;
};

struct pokeloc *pokelist;
spin_lock_t pokelistlock = SPIN_LOCK_INITIAILIZER;

/* Remember that data here on the disk has been modified. */
void
record_poke (vm_offset_t offset, vm_size_t length)
{
  struct pokeloc *pl = malloc (sizeof (struct pokeloc));
  pl->offset = trunc_page (offset);
  pl->length = round_page (offset + length) - pl->offset;

  spin_lock (&pokelistlock);
  pl->next = pokelist;
  pokelist = pl;
  spin_unlock (&pokelistlock);
}

/* Sync all the modified pieces of disk */
void
sync_disk (int wait)
{
  struct pokeloc *pl, *tmp;
  
  spin_lock (&pokelistlock);
  for (pl = pokelist; pl; pl = tmp)
    {
      pager_sync_some (diskpager->p, pl->offset, pl->length, wait);
      tmp = pl->next;
      free (pl);
    }
  pokelist = 0;
  spin_unlock (&pokelistlock);
}

