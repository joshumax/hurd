/* A data structure to remember modifications to a memory region

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

void pokel_init (struct pokel *pokel, struct pager *pager, void *image)
{
  pokel->lock = SPIN_LOCK_INITIALIZER;
  pokel->pokes = NULL;
  pokel->pager = pager;
  pokel->image = image;
}

/* Remember that data here on the disk has been modified. */
void
pokel_add (struct pokel *pokel, void *loc, vm_size_t length)
{
  struct poke *pl;
  vm_offset_t offset = trunc_page (loc - pokel->image);
  vm_offset_t end = round_page (loc + length - pokel->image);

  ext2_debug ("adding %p[%ul] (range 0x%x to 0x%x)", loc, length, offset, end);

  spin_lock (&pokel->lock);

  pl = pokel->pokes;
  while (pl != NULL)
    {
      vm_offset_t p_offs = pl->offset;
      vm_size_t p_end = p_offs + pl->length;

      if (p_offs == offset && p_end == end)
	break;
      else if (p_end >= offset && end >= p_offs)
	{
	  pl->offset = offset < p_offs ? offset : p_offs;
	  pl->length = (end > p_end ? end : p_end) - pl->offset;
	  ext2_debug ("extended 0x%x[%ul] to 0x%x[%ul]",
		      p_offs, p_end - p_offs, pl->offset, pl->length);
	  break;
	}

      pl = pl->next;
    }
  
  if (pl == NULL)
    {
      pl = pokel->free_pokes;
      if (pl == NULL)
	pl = malloc (sizeof (struct poke));
      else
	pokel->free_pokes = pl->next;
      pl->offset = offset;
      pl->length = end - offset;
      pl->next = pokel->pokes;
      pokel->pokes = pl;
    }

  spin_unlock (&pokel->lock);
}

/* Sync all the modified pieces of disk */
void
pokel_sync (struct pokel *pokel, int wait)
{
  struct poke *pl, *next;
  
  spin_lock (&pokel->lock);

  for (pl = pokel->pokes; pl; pl = next)
    {
      ext2_debug ("syncing 0x%x[%ul]", pl->offset, pl->length);
      pager_sync_some (pokel->pager, pl->offset, pl->length, wait);
      next = pl->next;
      pl->next = pokel->free_pokes;
      pokel->free_pokes = pl;
    }
  pokel->pokes = NULL;

  spin_unlock (&pokel->lock);
}
