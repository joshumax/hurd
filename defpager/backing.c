/* Backing store management for GNU Hurd.
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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


#include <hurd/store.h>

const struct store_class *const permitted_classes[] = 
{
  &store_device_class, &store_ileave_class, &store_concat_class, 0
};

/* Allocation map, by PAGE.  */  
/* If a bit is SET the corresponding PAGE is free. */
char *bmap;

/* Number of bytes in bmap */
size_t bmap_len;

/* Allocation rotor */
char *bmap_rotor;

pthread_mutex_t bmap_lock = PTHREAD_MUTEX_INITIALIZER;

error_t
init_backing (char *name)
{
  error_t err;
  int i;

  err = store_open (name, STORE_NO_FILEIO, &permitted_classes, &backing_store);
  if (err)
    return err;
  
  bmap_len = backing_store->size / vm_page_size / NBBY;
  bmap = malloc (bmap_len);
  for (i = 0; i < bmap_len; i++)
    bmap[i] = 0xff;
  bmap_rotor = bmap;

  /* Mark the very first page as occupied.  This makes sure we never
     return zero offsets from allocate_backing_page (which
     conventionally means that there is no space left.  It also makes
     sure we don't tromp on the misfeature in Linux of using the first
     page for permanent data. */
  *bmap_rotor |= 1;
}

int
allocate_backing_page ()
{
  int wrapped;
  int bit;
  int pfn;

  pthread_mutex_lock (&bmap_lock);

  wrapped = (bmap_rotor == bmap);

  while (!wrapped || bmap_rotor < bmap + bmap_len)
    {
      if (bmap[bmap_rotor])
	break;
      bmap_rotor++;
      if (bmap_rotor >= bmap + bmap_len)
	wrapped++;
    }
  
  if (wrapped == 2)
    {
      /* Didn't find one... */
      pthread_mutex_unlock (&bmap_lock);
      printf ("WARNING: Out of paging space; pageout failing.");
      return 0;
    }
  
  /* Find which bit */
  bit = ffs (*bmap_rotor);
  assert_backtrace (bit);
  bit--;
  
  /* Mark it */
  *bmap_rotor |= 1 << bit;
  
  /* Return the correct offset */
  pfn = (bmap_rotor - bmap) * 8 + bit;

  pthread_mutex_unlock (&bmap_lock);
  
  return pfn * (vm_page_size / store->block_size);
}


void
return_backing_pages (off_t *map, int maplen)
{
  int i;
  
  pthread_mutex_lock (&bmap_lock);
  for (i = 0; i < maplen; i++)
    {
      int pfn;
      char *b;
      int bit;

      pfn = map[i] / (vm_page_size / store->block_size);
      b = bmap + pfn & ~7;
      bit = pfn & 7;
      
      assert_backtrace ((*b & (1 << bit)) == 0);
      *b |= 1 << bit;
    }
  pthread_mutex_unlock (&bmap_lock);
}
