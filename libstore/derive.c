/* Calculation of various derived store fields

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

#include "store.h"

/* Fills in the values of the various fields in STORE that are derivable from
   the set of runs & the block size.  */
void
_store_derive (struct store *store)
{
  unsigned i;
  off_t *runs = store->runs;
  unsigned num_runs = store->num_runs;
  size_t bsize = store->block_size;

  /* BLOCK & SIZE */
  store->blocks = 0;

  for (i = 0; i < num_runs; i += 2)
    {
      store->wrap_src += runs[i + 1];
      if (runs[i] >= 0)
	store->blocks += runs[i + 1];
    }

  if (store->end == 0)
    /* END not set; set it using the info from RUNS.  */
    store->end = store->wrap_src;
  else if (store->wrap_src < store->end)
    /* A wrapped disk!  RUNS is repeated N times to reach END.  Adjust BLOCKS
       to include all iterations.  */
    {
      size_t num_iters = store->end / store->wrap_src;
      off_t last_part_base = num_iters * store->wrap_src;

      store->blocks *= num_iters;

      for (i = 0; i < num_runs; i += 2)
	if (last_part_base + runs[i + 1] < store->end)
	  {
	    store->blocks += store->end - (last_part_base + runs[i + 1]);
	    break;
	  }
	else if (runs[i] >= 0)
	  store->blocks += runs[i + 1];

      /* WRAP_DST must be set by the caller.  */
    }

  store->size = store->end * bsize;

  /* LOG2_BLOCK_SIZE */
  store->log2_block_size = 0;
  while ((1 << store->log2_block_size) < bsize)
    store->log2_block_size++;
  if ((1 << store->log2_block_size) != bsize)
    store->log2_block_size = 0;

  /* LOG2_BLOCKS_PER_PAGE */
  store->log2_blocks_per_page = 0;
  while ((bsize << store->log2_blocks_per_page) < vm_page_size)
    store->log2_blocks_per_page++;
  if ((bsize << store->log2_blocks_per_page) != vm_page_size)
    store->log2_blocks_per_page = 0;
}
