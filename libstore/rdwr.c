/* Store I/O

   Copyright (C) 1995, 96, 97, 98, 1999 Free Software Foundation, Inc.
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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>

#include "store.h"

/* Returns in RUN the tail of STORE's run list, who's first run contains
   ADDR, and is not a hole, and in RUNS_END a pointer pointing at the end of
   the run list.  Returns the offset within it at which ADDR occurs.  Also
   returns BASE, which should be added to offsets from RUNS.  */
static inline off_t
store_find_first_run (struct store *store, off_t addr,
		      struct store_run **run, struct store_run **runs_end,
		      off_t *base, size_t *index)
{
  struct store_run *tail = store->runs, *tail_end = tail + store->num_runs;
  off_t wrap_src = store->wrap_src;

  if (addr >= wrap_src && addr < store->end)
    /* Locate the correct position within a repeating pattern of runs.  */
    {
      *base = addr / store->wrap_dst;
      addr %= wrap_src;
    }
  else
    *base = 0;

  /* XXX: this isn't going to be very efficient if RUNS is very complex...
     But it should do dandy if it's short.  For long run lists, we could do a
     binary search or something.  */
  while (tail < tail_end)
    {
      off_t run_blocks = tail->length;

      if (run_blocks > addr)
	{
	  *run = tail;
	  *runs_end = tail_end;
	  *index = tail - store->runs;
	  return addr;
	}

      /* Not to the right place yet, move on...  */
      addr -= run_blocks;
      tail++;
    }

  return -1;
}

/* Update RUN, BASE, & INDEX to point to the next elemement in the runs
   array.  RUNS_END is the point where RUNS will wrap.  Returns true if
   things are still kosher.  */
static inline int
store_next_run (struct store *store, struct store_run *runs_end,
		struct store_run **run, off_t *base, size_t *index)
{
  (*run)++;
  (*index)++;

  if (*run == runs_end)
    /* Wrap around in a repeating RUNS.  */
    {
      *run = store->runs;
      *base += store->wrap_dst;
      *index = 0;
      return (*base < store->end);
    }
  else
    return 1;
}

/* Write LEN bytes from BUF to STORE at ADDR.  Returns the amount written
   in AMOUNT.  ADDR is in BLOCKS (as defined by STORE->block_size).  */
error_t
store_write (struct store *store,
	     off_t addr, void *buf, size_t len, size_t *amount)
{
  error_t err;
  size_t index;
  off_t base;
  struct store_run *run, *runs_end;
  int block_shift = store->log2_block_size;
  store_write_meth_t write = store->class->write;

  if (store->flags & STORE_READONLY)
    return EROFS;		/* XXX */

  addr = store_find_first_run (store, addr, &run, &runs_end, &base, &index);
  if (addr < 0)
    err = EIO;
  else if ((run->length << block_shift) >= len)
    /* The first run has it all... */
    err = (*write)(store, base + run->start + addr, index, buf, len, amount);
  else
    /* ARGH, we've got to split up the write ... */
    {
      mach_msg_type_number_t try = run->length << block_shift, written;

      /* Write the initial bit in the first run.  Errors here are returned.  */
      err = (*write)(store, base + run->start + addr, index, buf, try, &written);

      if (!err && written == try)
	/* Wrote the first bit successfully, now do the rest.  Any errors
	   will just result in a short write.  */
	{
	  buf += written;
	  len -= written;

	  while (store_next_run (store, runs_end, &run, &base, &index)
		 && run->start >= 0) /* Check for holes.  */
	    /* Ok, we can write in this run, at least a bit.  */
	    {
	      mach_msg_type_number_t seg_written;
	      off_t run_len = (run->length << block_shift);

	      try = run_len > len ? len : run_len;
	      err = (*write)(store, base + run->start, index, buf, try,
			     &seg_written);
	      if (err)
		break;	/* Ack */

	      written += seg_written;
	      len -= seg_written;
	      if (len == 0)
		break;	/* Nothing left to write!  */

	      if (seg_written < run_len)
		break;	/* Didn't use up the run, we're done.  */

	      buf += seg_written;
	    }
	}

      *amount = written;
    }

  return err;
}

/* Read AMOUNT bytes from STORE at ADDR into BUF & LEN (which following the
   usual mach buffer-return semantics) to STORE at ADDR.  ADDR is in BLOCKS
   (as defined by STORE->block_size).  */
error_t
store_read (struct store *store,
	    off_t addr, size_t amount, void **buf, size_t *len)
{
  size_t index;
  off_t base, first_run_size;
  struct store_run *run, *runs_end;
  int block_shift = store->log2_block_size;
  store_read_meth_t read = store->class->read;

  addr = store_find_first_run (store, addr, &run, &runs_end, &base, &index);
  if (addr < 0 || run->start < 0)
    return EIO;			/* Reading from a hole.  */

  first_run_size = (run->length << block_shift);
  if (first_run_size >= amount)
    /* The first run has it all... */
    {
      if (first_run_size < amount)
	amount = first_run_size;
      return (*read)(store, base + run->start + addr, index, amount, buf, len);
    }
  else
    /* ARGH, we've got to split up the read ... This isn't fun. */
    {
      error_t err;
      int all;
      /* WHOLE_BUF and WHOLE_BUF_LEN will point to a buff that's large enough
	 to hold the entire request.  This is initially whatever the user
	 passed in, but we'll change it as necessary.  */
      void *whole_buf = *buf, *buf_end;
      size_t whole_buf_len = *len;

      /* Read LEN bytes from the store address ADDR into BUF_END.  BUF_END
	 and AMOUNT are adjusted by the amount actually read.  Whether or not
	 the amount read is the same as what was request is returned in ALL. */
      inline error_t seg_read (off_t addr, off_t len, int *all)
	{
	  /* SEG_BUF and SEG_LEN are the buffer for a particular bit of the
	     whole (within one run). */
	  void *seg_buf = buf_end;
	  size_t seg_buf_len = len;
	  error_t err =
	    (*read)(store, addr, index, len, &seg_buf, &seg_buf_len);
	  if (!err)
	    {
	      /* If for some bizarre reason, the underlying storage chose not
		 to use the buffer space we so kindly gave it, bcopy it to
		 that space.  */
	      if (seg_buf != buf_end)
		{
		  bcopy (seg_buf, buf_end, seg_buf_len);
		  munmap (seg_buf, seg_buf_len);
		}
	      buf_end += seg_buf_len;
	      amount -= seg_buf_len;
	      *all = (seg_buf_len == len);
	    }
	  return err;
	}

      if (whole_buf_len < amount)
	/* Not enough room in the user's buffer to hold everything, better
	   make room.  */
	{
	  whole_buf_len = amount;
	  err = vm_allocate (mach_task_self (),
			     (vm_address_t *)&whole_buf, amount, 1);
	  if (err)
	    return err;		/* Punt early, there's nothing to clean up.  */
	}

      buf_end = whole_buf;

      err = seg_read (base + run->start + addr, first_run_size, &all);
      while (!err && all && amount > 0
	     && store_next_run (store, runs_end, &run, &base, &index))
	{
	  off_t run_addr = run->start;
	  off_t run_blocks = run->length;

	  if (run_addr < 0)
	    /* A hole!  Can't read here.  Must stop.  */
	    break;
	  else
	    {
	      off_t run_len = (run_blocks << block_shift);
	      off_t seg_len = run_len > amount ? amount : run_len;
	      err = seg_read (base + run_addr, seg_len, &all);
	    }
	}

      /* The actual amount read.  */
      *len = buf_end - whole_buf;
      if (*len > 0)
	err = 0;		/* Return a short read instead of an error.  */

      /* Deallocate any amount of WHOLE_BUF we didn't use.  */
      if (whole_buf != *buf)
	{
	  if (err)
	    munmap (whole_buf, whole_buf_len);
	  else
	    {
	      vm_size_t unused = whole_buf_len - round_page (*len);
	      if (unused)
		munmap (whole_buf + whole_buf_len - unused, unused);
	      *buf = whole_buf;
	    }
	}

      return err;
    }
}
