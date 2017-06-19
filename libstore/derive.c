/* Calculation of various derived store fields

   Copyright (C) 1995-97,2001 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <assert-backtrace.h>
#include <sys/types.h>
#include <mach.h>

#include "store.h"

/* Fills in the values of the various fields in STORE that are derivable from
   the set of runs & the block size.  */
void
_store_derive (struct store *store)
{
  unsigned i;
  struct store_run *runs = store->runs;
  unsigned num_runs = store->num_runs;
  size_t bsize = store->block_size;

  /* BLOCK & SIZE */
  store->blocks = 0;
  store->wrap_src = 0;

  for (i = 0; i < num_runs; i++)
    {
      store->wrap_src += runs[i].length;
      if (runs[i].start >= 0)	/* Not a hole */
	store->blocks += runs[i].length;
    }

  if (store->end == 0)
    /* END not set; set it using the info from RUNS.  */
    store->end = store->wrap_src;
  else if (store->wrap_src < store->end)
    /* A wrapped disk!  RUNS is repeated N times to reach END.  Adjust BLOCKS
       to include all iterations.  */
    {
      size_t num_iters = store->end / store->wrap_src;
      store_offset_t last_part_base = num_iters * store->wrap_src;

      store->blocks *= num_iters;

      for (i = 0; i < num_runs; i++)
	if (last_part_base + runs[i].length < store->end)
	  {
	    store->blocks += store->end - (last_part_base + runs[i].length);
	    break;
	  }
	else if (runs[i].start >= 0)
	  store->blocks += runs[i].length;

      /* WRAP_DST must be set by the caller.  */
    }

  store->size = store->end * bsize;

  store->log2_block_size = 0;
  store->log2_blocks_per_page = 0;

  if (bsize != 0)
    {
      while ((1 << store->log2_block_size) < bsize)
	store->log2_block_size++;
      assert_backtrace ((1 << store->log2_block_size) == bsize);

      while ((bsize << store->log2_blocks_per_page) < vm_page_size)
	store->log2_blocks_per_page++;
      assert_backtrace ((bsize << store->log2_blocks_per_page) == vm_page_size);
    }
}
