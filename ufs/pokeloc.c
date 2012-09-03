/* Remember where we've written the disk to speed up sync
   Copyright (C) 1994, 1996 Free Software Foundation, Inc.
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
pthread_spinlock_t pokelistlock = PTHREAD_SPINLOCK_INITIALIZER;

/* Remember that data here on the disk has been modified. */
void
record_poke (void *loc, vm_size_t length)
{
  struct pokeloc *pl = malloc (sizeof (struct pokeloc));
  vm_offset_t offset;

  offset = loc - disk_image;
  pl->offset = trunc_page (offset);
  pl->length = round_page (offset + length) - pl->offset;

  pthread_spin_lock (&pokelistlock);
  pl->next = pokelist;
  pokelist = pl;
  pthread_spin_unlock (&pokelistlock);
}

/* Get rid of any outstanding pokes.  */
void
flush_pokes ()
{
  struct pokeloc *pl;

  pthread_spin_lock (&pokelistlock);
  pl = pokelist;
  pokelist = 0;
  pthread_spin_unlock (&pokelistlock);

  while (pl)
    {
      struct pokeloc *next = pl->next;
      free (pl);
      pl = next;
    }
}

/* Sync all the modified pieces of disk */
void
sync_disk (int wait)
{
  struct pokeloc *pl, *tmp;

  pthread_spin_lock (&pokelistlock);
  for (pl = pokelist; pl; pl = tmp)
    {
      pager_sync_some (diskfs_disk_pager, pl->offset, pl->length, wait);
      tmp = pl->next;
      free (pl);
    }
  pokelist = 0;
  pthread_spin_unlock (&pokelistlock);
}

