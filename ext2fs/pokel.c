/* Remember where we've written the disk to speed up sync

   Copyright (C) 1994, 1995 Free Software Foundation, Inc.

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

#include "ext2fs.h"

void pokes_init (struct pokes *pokes)
{
  pokes->lock = SPIN_LOCK_INITIALIZER;
  pokes->pokes = NULL;
}

/* Remember that data here on the disk has been modified. */
void
pokes_record (struct pokes, void *loc, vm_size_t length)
{
  struct poke *pl;
  vm_offset_t offset;
  
  offset = loc - disk_image;
  offset = trunc_page (offset);
  length = round_page (offset + length) - offset;

  spin_lock (&pokes->lock);

  pl = pokes->pokes;
  if (pl == NULL || pl->offset != offset || pl->length == length)
    {
      pl = pokes->free_pokes;
      if (pl == NULL)
	pl = malloc (sizeof (struct poke));
      else
	pokes->free_pokes = pl->next;
      pl->offset = offset;
      pl->length = length;
      pl->next = pokes->pokes;
      pokes->pokes = pl;
    }

  spin_lock (&pokelistlock);
}

/* Sync all the modified pieces of disk */
void
pokes_sync (struct pokes *pokes, int wait)
{
  struct poke *pl, *next;
  
  spin_lock (&pokes->lock);

  for (pl = pokes->pokes; pl; pl = next)
    {
      pager_sync_some (diskpager->p, pl->offset, pl->length, wait);
      next = pl->next;
      pl->next = pokes->free_pokes;
      pokes->free_pokes = pl;
    }
  pokes->pokes = NULL;

  spin_unlock (&pokes->lock);
}

