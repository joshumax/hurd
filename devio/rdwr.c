/* Implements various types of I/O on top of raw devices.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <hurd.h>
#include <assert.h>
#include <string.h>

#include "open.h"
#include "dev.h"
#include "mem.h"
#include "window.h"

/* ---------------------------------------------------------------- */

/* Writes BUF to DEV, copying through an intermediate buffer to page-align
   it.  If STAGING_BUF isn't 0, it is used as the copy buffer for
   small-enough transfers (staging_buf is assumed to be one block in length).
   AMOUNT is the actual amount written, and LEN is the amount of source
   material actually in BUF; if LEN is smaller than AMOUNT, the remainder is
   zero.  */
static error_t
copying_block_write(struct dev *dev, vm_address_t staging_buf,
		    vm_address_t buf, vm_size_t len, vm_size_t amount,
		    vm_offset_t *offs)
{
  error_t err = 0;
  vm_address_t copy_buf = staging_buf;
  vm_size_t copy_buf_len = dev->block_size;

  if (amount > dev->block_size || staging_buf == 0)
    {
      copy_buf_len = amount;
      err = vm_allocate(mach_task_self(), &copy_buf, copy_buf_len, 1);
      if (err)
	return err;
    }

  bcopy((char *)buf, (char *)copy_buf, len);
  if (len < amount && copy_buf == staging_buf)
    /* We need to zero the rest of the bloc, but only if we didn't
       vm_allocate it (in which case it  will be zero-filled).  */
    bzero((char *)buf + len, amount - len);

  err = dev_write(dev, copy_buf, amount, offs);

  if (copy_buf != staging_buf)
    vm_deallocate(mach_task_self(), copy_buf, copy_buf_len);

  return err;
}

/* ---------------------------------------------------------------- */

/* Copies LEN bytes from BUF to DEV, using STAGING_BUF to do buffering of
   partial blocks, and returning the amount actually written in AMOUNT.
   *OFFS is incremented to reflect the amount read/written.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t
buffered_write(struct dev *dev, vm_address_t staging_buf,
	       vm_address_t buf, vm_size_t len, vm_size_t *amount,
	       vm_offset_t *offs)
{
  error_t err = 0;
  int bsize = dev->block_size;
  int staging_buf_loc = *offs % bsize;
  int left_in_staging_buf = bsize - staging_buf_loc;
  vm_offset_t start_offs = *offs;

  if (left_in_staging_buf > 0)
    /* Write what's buffered from the last I/O.  */
    {
      /* Amount of the current i/o we can put in the staging buffer.  */
      int stage = (left_in_staging_buf > len ? len : left_in_staging_buf);

      bcopy((char *)buf, (char *)staging_buf + staging_buf_loc, stage);
	  
      buf += stage;
      len -= stage;
      *offs += stage;

      if (stage == left_in_staging_buf)
	/* We've filled up STAGING_BUF so we can write it out now.  */
	{
	  /* Backup OFFS to reflect the beginning-of-block position.  */
	  *offs -= bsize;
	  err = dev_write(dev, staging_buf, bsize, offs);
	}
    }

  if (!err && len > bsize)
    /* Enough i/o pending to do whole block transfers.  */
    {
      /* The number of bytes at the end of the transfer that aren't a
	 multiple of the block-size.  We have to deal with these separately
	 because device i/o must be in block multiples.  */
      int excess = len % bsize;
      vm_size_t block_len = len - excess;

      if (dev_write_valid(dev, buf, block_len, offs))
	/* BUF is page-aligned, so we can do i/o directly to the device, or
	   it is small enough that it doesn't matter.  */
	err = dev_write(dev, buf, block_len, offs);
      else
	/* Argh!  BUF isn't page aligned!  We must filter the i/o though an
	   intermediate buffer...  */
	err = copying_block_write(dev, staging_buf,
				  buf, block_len, block_len, offs);

      if (*offs - start_offs < left_in_staging_buf + block_len)
	/* Didn't write out all the blocks, so suppress buffering the rest. */
	len = 0;
      else
	len = excess;
    }

  /* At this point, LEN should be < BLOCK_SIZE, so we use buffering again. */
  if (!err && len > 0)
    {
      bcopy((char *)staging_buf, (char *)buf, len);
      *offs += len;
    }

  *amount = *offs - start_offs;
  if (*amount > 0)
    /* If an error occurred, but we successfully wrote *something*, then
       pretend nothing bad happened; the error will probably get caught next
       time.  */
    err = 0;

  return err;
}

/* ---------------------------------------------------------------- */

/* Reads AMOUNT bytes from DEV and returns them in BUF and BUF_LEN (using the
   standard mach out-array conventions), using STAGING_BUF to do buffering of
   partial blocks.  *OFFS is incremented to reflect the amount read/written.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
buffered_read (struct dev *dev, vm_address_t staging_buf,
	       vm_address_t *buf, vm_size_t *buf_len, vm_size_t amount,
	       vm_offset_t *offs)
{
  error_t err = 0;
  int bsize = dev->block_size;
  vm_offset_t start_offs = *offs;
  int staging_buf_loc = *offs % bsize;
  int from_staging_buf = bsize - staging_buf_loc;
  vm_address_t block_buf = *buf;
  vm_size_t block_buf_size = *buf_len;
  vm_size_t block_amount = amount;

  if (staging_buf_loc > 0)
    {
      /* Read into a temporary buffer.  */
      block_buf = 0;
      block_buf_size = 0;

      if (from_staging_buf > amount)
	from_staging_buf = amount;

      block_amount -= from_staging_buf;
    }
  else
    from_staging_buf = 0;

  /* Read any new block required.  */
  if (block_amount > 0)
    {
      /* We read enough to get every full block of BLOCK_AMOUNT, plus an
	 additional whole block if there's any more; we just copy any excess
	 from that last block into STAGING_BUF for next time.  */
      block_amount = ((block_amount + bsize - 1) / bsize) * bsize;

      err = dev_read(dev, &block_buf, &block_buf_size, block_amount, offs);
      if (err && staging_buf_loc > 0)
	/* We got an error, but don't abort, since we did get the bit from
	   the buffer.  */
	{
	  err = 0;
	  amount = from_staging_buf;
	  block_amount = 0;
	}

      if (amount > *offs - start_offs)
	/* If we read less than we hoped, reflect this down below.  */
	amount = *offs - start_offs;
    }

  if (staging_buf_loc > 0)
    /* Coalesce what we have in STAGING_BUF with what we read.  */
    {
      err = allocate(buf, buf_len, amount);
      assert_perror(err);
      bcopy((char *)staging_buf + staging_buf_loc, (char *)*buf,
	    from_staging_buf);

      if (block_amount > 0)
	bcopy((char *)block_buf, (char *)*buf + from_staging_buf,
	      amount - from_staging_buf);
    }
  else
    /* Otherwise, BLOCK_BUF should already contain the correct data.  */
    {
      *buf = block_buf;
      *buf_len = block_buf_size;
    }

  if (*offs - start_offs > amount)
    /* We've read too far, so put some amount from the end back into
       STAGING_BUF.  */
    {
      int excess = (*offs - start_offs) - amount;

      bcopy((char *)block_buf + amount,
	    (char *)staging_buf + bsize - excess,
	    excess);
      *offs -= excess;

      if (excess >= vm_page_size)
	deallocate_excess(*buf, *buf_len, excess);
      *buf_len -= excess;
    }

  /* Deallocate any extra copy buffer if necessary.  */
  if (*buf != block_buf)
    vm_deallocate(mach_task_self(), block_buf, block_buf_size);

  return err;
}

/* ---------------------------------------------------------------- */

/* Write BUF_LEN bytes from BUF to DEV, padding with zeros as necessary to
   write whole blocks, and returning the amount actually written in AMOUNT.
   If successful, 0 is returned, otherwise an error code is returned.  *OFFS
   is incremented by the change in device location.  */
error_t
raw_write(struct dev *dev,
	  vm_address_t buf, vm_size_t buf_len,
	  vm_size_t *amount, vm_offset_t *offs)
{
  error_t err;
  int bsize = dev->block_size;
  int block_amount = ((buf_len + bsize - 1) / bsize) * bsize;
  vm_offset_t start_offs = *offs;

  if (start_offs % bsize != 0)
    return EINVAL;

  if (block_amount == buf_len && dev_write_valid(dev, buf, block_amount, offs))
    /* BUF is page-aligned, so we can do i/o directly to the device, or
       it is small enough that it doesn't matter.  */
    err = dev_write(dev, buf, block_amount, offs);
  else
    /* Argh!  BUF isn't page aligned!  We must filter the i/o though an
       intermediate buffer...  [We use DEV's io_state buffer, as we know
       that the io_state is locked in open_rdwr, and it isn't otherwise
       used...]  */
    err = copying_block_write(dev, dev->io_state.buffer,
			      buf, buf_len, block_amount, offs);

  if (!err && *offs - start_offs < buf_len)
    *amount = *offs - start_offs;
  else
    *amount = buf_len;

  return err;
}

/* Read AMOUNT bytes from DEV into BUF and BUF_LEN; only whole blocks are
   read, but anything greater than *AMOUNT bytes is discarded.  The standard
   mach out-array convention is used to return the data in BUF and BUF_LEN.
   If successful, 0 is returned, otherwise an error code is returned.  *OFFS
   is incremented by the change in device location.  */
error_t
raw_read(struct dev *dev,
	 vm_address_t *buf, vm_size_t *buf_len,
	 vm_size_t amount, vm_offset_t *offs)
{
  error_t err;
  int bsize = dev->block_size;
  int block_amount = ((amount + bsize - 1) / bsize) * bsize;

  if (*offs % bsize != 0)
    return EINVAL;

  err = dev_read(dev, buf, buf_len, block_amount, offs);
  if (!err)
    {
      int excess = *buf_len - amount;
      if (excess > vm_page_size)
	deallocate_excess(*buf, *buf_len, excess);
      if (excess > 0)
	*buf_len = amount;
    }

  return err;
}

/* ---------------------------------------------------------------- */

struct rdwr_state
{
  struct dev *dev;
  off_t user_offs;
  vm_offset_t *offs_p;
  struct io_state *io_state;
};

/* Setup state needed for I/O to/from OPEN, putting it into STATE.  OFFS
   should be the original user-supplied offset.  */
static void
rdwr_state_init(struct rdwr_state *state, struct open *open, off_t offs)
{
  state->dev = open->dev;
  state->io_state = open_get_io_state(open);
  state->user_offs = offs;

  if (dev_is(state->dev, DEV_SERIAL))
    /* For serial i/o, we always ignore the proffered offs, and use the
       actual device offset.  */
    state->user_offs = -1;

  if (state->user_offs == -1 || !dev_is(state->dev, DEV_BUFFERED))
    /* If we're going to use some bit of IO_STATE, lock it first.  This
       should only not happen if we're going to used windowed i/o with an
       explicit offset.  */
    io_state_lock(state->io_state);

  if (state->user_offs == -1)
    state->offs_p = &state->io_state->location;
  else
    state->offs_p = (vm_offset_t *)&state->user_offs;
}

/* Destroy any state created by rdwr_state_init.  */
static void
rdwr_state_finalize(struct rdwr_state *state)
{
  if (state->user_offs == -1 || !dev_is(state->dev, DEV_BUFFERED))
    io_state_unlock(state->io_state);
}

/* ---------------------------------------------------------------- */

/* Writes up to LEN bytes from BUF to OPEN's device at device offset OFFS
   (which may be ignored if the device doesn't support random access),
   and returns the number of bytes written in AMOUNT.  If no error occurs,
   zero is returned, otherwise the error code is returned.  */
error_t
open_write(struct open *open, vm_address_t buf, vm_size_t len,
	   vm_size_t *amount, off_t offs)
{
  error_t err;
  struct rdwr_state state;
  struct dev *dev = open->dev;

  rdwr_state_init(&state, open, offs);

  offs = *state.offs_p;
  if (offs < 0)
    err = EINVAL;
  if (offs + len > dev->size)
    err = EIO;
  else if (!dev_is(dev, DEV_BUFFERED))
    err = raw_write(dev, buf, len, amount, state.offs_p);
  else if (dev_is(dev, DEV_SERIAL))
    {
      state.io_state->buffer_use = IO_STATE_BUFFERED_WRITE;
      err = buffered_write(dev, state.io_state->buffer, buf, len,
			   amount, state.offs_p);
    }
  else
    err = window_write(open->window, buf, len, amount, state.offs_p);

  rdwr_state_finalize(&state);

  return err;
}

/* Reads up to AMOUNT bytes from the device into BUF and BUF_LEN using the
   standard mach out-array convention.  If no error occurs, zero is returned,
   otherwise the error code is returned.  */
error_t
open_read(struct open *open, vm_address_t *buf, vm_size_t *buf_len,
	  vm_size_t amount, off_t offs)
{
  error_t err;
  struct rdwr_state state;
  struct dev *dev = open->dev;

  rdwr_state_init(&state, open, offs);

  offs = *state.offs_p;
  if (offs < 0)
    err = EINVAL;
  if (offs + amount > dev->size)
    err = EIO;
  else if (!dev_is(dev, DEV_BUFFERED))
    err = raw_read(dev, buf, buf_len, amount, state.offs_p);
  else if (dev_is(dev, DEV_SERIAL))
    {
      state.io_state->buffer_use = IO_STATE_BUFFERED_READ;
      err = buffered_read(dev, state.io_state->buffer, buf, buf_len,
			  amount, state.offs_p);
    }
  else
    err = window_read(open->window, buf, buf_len, amount, state.offs_p);

  rdwr_state_finalize(&state);

  return err;
}

/* Set OPEN's location to OFFS, interpreted according to WHENCE as by seek.
   The new absolute location is returned in NEW_OFFS (and may not be the same
   as OFFS).  If no error occurs, zero is returned, otherwise the error code
   is returned.  */
error_t
open_seek (struct open *open, off_t offs, int whence, off_t *new_offs)
{
  error_t err = 0;
  struct io_state *io_state = open_get_io_state (open);

  if (!dev_is (open->dev, DEV_SEEKABLE))
    return ESPIPE;

  io_state_lock (io_state);

  switch (whence)
    {
    case SEEK_SET:
      *new_offs = offs; break;
    case SEEK_CUR:
      *new_offs = io_state->location + offs; break;
    case SEEK_END:
      *new_offs = open->dev->size - offs; break;
    default:
      err = EINVAL;
    }

  if (!err)
    {
      if (!dev_is (open->dev, DEV_BUFFERED))
	/* On unbuffered devices force seeks to the nearest block boundary.  */
	*new_offs -= *new_offs % open->dev->block_size;
      io_state->location = *new_offs;
    }

  io_state_unlock (io_state);

  return err;
}
