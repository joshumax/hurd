/* Store I/O

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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA. */

#include <string.h>

#include "store.h"

/* Returns in RUNS the tail of STORE's run list, who's first run contains
   ADDR, and is not a whole, and in RUNS_END a pointer pointing at the end of
   the run list.  Returns the offset within it at which ADDR occurs.  */
static inline off_t
store_find_first_run (struct store *store, off_t addr,
		      off_t **runs, off_t **runs_end)
{
  off_t *tail = store->runs, *tail_end = tail + store->runs_len;

  /* XXX: this isn't going to be very efficient if RUNS is very complex...
     But it should do dandy if it's short.  For long run lists, we could do a
     binary search or something.  */
  while (tail < tail_end)
    {
      off_t run_blocks = tail[1];

      if (run_blocks > addr)
	{
	  *runs = tail;
	  *runs_end = tail_end;
	  return addr;
	}

      /* Not to the right place yet, move on...  */
      addr -= run_blocks;
      tail += 2;
    }

  return -1;
}

/* Write LEN bytes from BUF to STORE at ADDR.  Returns the amount written
   in AMOUNT.  ADDR is in BLOCKS (as defined by STORE->block_size).  */
error_t
store_write (struct store *store,
	     off_t addr, char *buf, size_t len, size_t *amount)
{
  error_t err;
  off_t *runs, *runs_end;
  store_write_meth_t write = store->meths->write;

  addr = store_find_first_run (store, addr, &runs, &runs_end);
  if (addr < 0)
    err = EIO;
  else if (runs[1] >= len)
    /* The first run has it all... */
    err = (*write)(store, runs[0] + addr, buf, len, amount);
  else
    /* ARGH, we've got to split up the write ... */
    {
      mach_msg_type_number_t written;

      /* Write the initial bit in the first run.  Errors here are returned.  */
      err = (*write)(store, runs[0] + addr, buf, runs[1], &written);

      if (!err && written == runs[1])
	/* Wrote the first bit successfully, now do the rest.  Any errors
	   will just result in a short write.  */
	{
	  int block_shift = store->log2_block_size;

	  buf += written;
	  len -= written;

	  runs += 2;
	  while (runs != runs_end)
	    {
	      off_t run_addr = runs[0];
	      off_t run_blocks = runs[1];

	      if (run_addr < 0)
		/* A hole!  Can't write here.  Must stop.  */
		break;
	      else
		/* Ok, we can write in this run, at least a bit.  */
		{
		  mach_msg_type_number_t seg_written;
		  off_t run_len = (run_blocks << block_shift);
		  size_t seg_len = run_len > len ? len : run_len;

		  err = (*write)(store, run_addr, buf, seg_len, &seg_written);
		  if (err)
		    break;	/* Ack */

		  written += seg_written;
		  if (seg_written < run_len)
		    break;	/* Didn't use up the run, we're done.  */

		  len -= seg_written;
		  if (len == 0)
		    break;	/* Nothing left to write!  */

		  buf += written;
		}

	      runs += 2;
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
	    off_t addr, size_t amount, char **buf, size_t *len)
{
  error_t err;
  off_t *runs, *runs_end;
  store_read_meth_t read = store->meths->read;

  addr = store_find_first_run (store, addr, &runs, &runs_end);
  if (addr < 0)
    err = EIO;
  else if (runs[1] >= amount)
    /* The first run has it all... */
    err = (*read)(store, runs[0] + addr, amount, buf, len);
  else
    /* ARGH, we've got to split up the read ... This isn't fun. */
    {
      int all;
      /* WHOLE_BUF and WHOLE_BUF_LEN will point to a buff that's large enough
	 to hold the entire request.  This is initially whatever the user
	 passed in, but we'll change it as necessary.  */
      char *whole_buf = *buf, *buf_end = whole_buf;
      size_t whole_buf_len = *len;
      int block_shift = store->log2_block_size;

      /* Read LEN bytes from the store address ADDR into BUF_END.  BUF_END
	 and AMOUNT are adjusted by the amount actually read.  Whether or not
	 the amount read is the same as what was request is returned in ALL. */
      inline error_t seg_read (off_t addr, off_t len, int *all)
	{
	  /* SEG_BUF and SEG_LEN are the buffer for a particular bit of the
	     whole (within one run). */
	  char *seg_buf = buf_end;
	  size_t seg_buf_len = len;
	  error_t err = (*read)(store, addr, len, &seg_buf, &seg_buf_len);
	  if (!err)
	    {
	      /* If for some bizarre reason, the underlying storage chose not
		 to use the buffer space we so kindly gave it, bcopy it to
		 that space.  */
	      if (seg_buf != buf_end)
		bcopy (seg_buf, buf_end, seg_buf_len);
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

      err = seg_read (runs[0] + addr, runs[1] << block_shift, &all);

      if (!err && all)
	{
	  runs += 2;
	  while (!err && runs != runs_end && all)
	    {
	      off_t run_addr = runs[0];
	      off_t run_blocks = runs[1];

	      if (run_addr < 0)
		/* A hole!  Can't read here.  Must stop.  */
		break;
	      else if (amount == 0)
		break;
	      else
		{
		  off_t run_len = (run_blocks << block_shift);
		  off_t seg_len = run_len > amount ? amount : run_len;
		  err = seg_read (run_addr, seg_len, &all);
		}

	      runs +=2;
	    }
	}

      /* The actual amount read.  */
      *len = whole_buf + whole_buf_len - buf_end;
      if (*len > 0)
	err = 0;		/* Return a short read instead of an error.  */

      /* Deallocate any amount of WHOLE_BUF we didn't use.  */
      if (whole_buf != *buf)
	if (err)
	  vm_deallocate (mach_task_self (),
			 (vm_address_t)whole_buf, whole_buf_len);
	else
	  {
	    vm_size_t unused = whole_buf_len - round_page (*len);
	    if (unused)
	      vm_deallocate (mach_task_self (),
			     (vm_address_t)whole_buf + whole_buf_len - unused,
			     unused);
	    *buf = whole_buf;
	  }
    }

  return err;
}
		
