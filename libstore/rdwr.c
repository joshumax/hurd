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

/* Write LEN bytes from BUF to STORE at ADDR.  Returns the amount written in
   AMOUNT.  ADDR is in BLOCKS (as defined by store->block_size).  */
error_t
store_write (struct store *store,
	     off_t addr, char *buf, size_t len, size_t *amount);
{
  error_t err = 0;
  size_t request_len = len;
  off_t *runs = store->runs;
  unsigned runs_len = store->runs_len;
  store_write_meth_t write = store->meths->write;
  int block_shift = store->log2_block_size;

  /* XXX: this isn't going to be very efficient if RUNS is very complex...
     But it should do dandy if it's short.  For long run lists, we could do a
     binary search or something to find the starting run.  */
  while (runs_len)
    {
      off_t run_offs = runs[0];
      off_t run_len = runs[1];

      if (run_len <= addr)
	/* Not to the right place yet, move on...  */
	addr -= run_len;
      else if (run_offs < 0)
	/* A hole!  Can't write here.  Must stop.  */
	break;
      else
	/* Ok, we can write in this run, at least a bit.  */
	{
	  size_t written, blocks_written;
	  off_t end = addr + len;
	  off_t seg_len = end > run_len ? run_len - addr : len;

	  /* Write to the actual object at the correct address.  */
	  err = (*write)(store, run_offs + addr, buf, seg_len, &written);
	  if (err)
	    /* Ack */
	    break;

	  buf += written;
	  len -= written;

	  blocks_written = written >> block_shift;
	  if ((blocks_written << block_shift) != seg_amount)
	    /* A non-block multiple amount was written!?  What do we do?  */
	    break;

	  addr += blocks_written;
	}

      runs += 2;
      runs_len -= 2;
    }

  if (!err)
    *amount = request_len - len;

  return err;
}

error_t
store_read (struct store *store,
	    off_t addr, size_t amount, char **buf, size_t *len)
{
}
