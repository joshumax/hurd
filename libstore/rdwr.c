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

/* Write LEN bytes from BUF to STORE at ADDR.  If AMOUNT is NULL, returns EIO
   if less than LEN bytes were written, otherwise returns the amount written
   in AMOUNT.  ADDR is in BLOCKS (as defined by store->block_size).  */
error_t
store_write (struct store *store,
	     off_t addr, char *buf, size_t len, size_t *amount);
{
  error_t err = EIO;		/* What happens if we run off the end */
  size_t total_written = 0;
  off_t *runs = store->runs;
  unsigned runs_len = store->runs_len;
  store_write_meth_t write = store->meths->write;
  int block_shift = store->log2_block_size;

  /* XXX: this isn't going to be very efficient if RUNS is very complex...
     But it should do dandy if it's short.  For long run lists, we could do a
     binary search or something to find the starting run.  */
  while (runs_len)
    {
      off_t run_addr = runs[0];
      off_t run_blocks = runs[1];

      if (run_blocks <= addr)
	/* Not to the right place yet, move on...  */
	addr -= run_blocks;
      else if (run_addr < 0)
	/* A hole!  Can't write here.  Must stop.  If no data's been written
	   so far, ERR will still contain EIO, otherwise it will contain 0
	   and we'll just return a short write.  */
	break;
      else
	/* Ok, we can write in this run, at least a bit.  */
	{
	  size_t written, blocks_written;
	  off_t run_len = (run_blocks << block_shift);
	  off_t end = (addr << block_shift) + len;
	  off_t seg_len = end > run_len ? run_len - addr : len;

	  /* Write to the actual object at the correct address.  */
	  err = (*write)(store, run_addr + addr, buf, seg_len, &written);
	  if (err)
	    /* Ack */
	    break;

	  total_written += written;

	  len -= written;
	  if (len == 0)
	    break;		/* All data written */

	  buf += written;

	  blocks_written = written >> block_shift;
	  if ((blocks_written << block_shift) != seg_amount)
	    /* A non-block multiple amount was written!?  What do we do?  */
	    break;

	  addr += blocks_written;
	}

      runs += 2;
      runs_len -= 2;
    }

  if (amount)
    /* The user wants to know about short writes.  */
    {
      if (total_written)
	err = 0;		/* return a short write */
      *amount = total_written;
    }
  else if (!err)
    /* Since there's no way to return the amount actually written, signal an
       error.  */
    err = EIO;

  return err;
}

error_t
store_read (struct store *store,
	    off_t addr, size_t amount, char **buf, size_t *len)
{
  error_t err = EIO;		/* What happens if we run off the end */
  size_t total_read = 0;
  off_t *runs = store->runs;
  unsigned runs_len = store->runs_len;
  store_read_meth_t read = store->meths->read;
  int block_shift = store->log2_block_size;

  /* XXX: this isn't going to be very efficient if RUNS is very complex...
     But it should do dandy if it's short.  For long run lists, we could do a
     binary search or something to find the starting run.  */
  while (runs_len)
    {
      off_t run_addr = runs[0];
      off_t run_blocks = runs[1];

      if (run_blocks <= addr)
	/* Not to the right place yet, move on...  */
	addr -= run_blocks;
      else if (run_addr < 0)
	/* A hole!  Can't read here.  Must stop.  If no data's been read
	   so far, ERR will still contain EIO, otherwise it will contain 0
	   and we'll just return a short read.  */
	break;
      else
	/* Ok, we can read in this run, at least a bit.  */
	{
	  size_t seg_read, blocks_read;
	  off_t run_len = (run_blocks << block_shift);
	  off_t end = (addr << block_shift) + amount;
	  off_t seg_amount = end > run_len ? run_len - addr : amount;

	  /* Read to the actual object at the correct address.  */
	  if (total_read)
	    /* Some stuff has already been read, so we have to worry about
	       coalescing the return buffers.  */
	    {
	      
	    }
	  else
	    {
	      err = (*read)(store, run_addr + addr, seg_amount, buf, len);
	      if (err)
		/* Ack */
		break;
	      seg_read = len;
	    }

	  total_read += seg_read;

	  amount -= seg_read;
	  if (amount == 0)
	    break;		/* All data read */

	  blocks_read = seg_read >> block_shift;
	  if ((blocks_read << block_shift) != seg_amount)
	    /* A non-block multiple amount was read!?  What do we do?  */
	    break;

	  addr += blocks_read;
	}

      runs += 2;
      runs_len -= 2;
    }

  if (amount)
    /* The user wants to know about short reads.  */
    {
      if (total_read)
	err = 0;		/* return a short read!! */
      *amount = total_read;
    }
  else if (!err)
    /* Since there's no way to return the amount actually read, signal an
       error.  */
    err = EIO;

  return err;
}
