/* store `device' I/O

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
#include <hurd/pager.h>
#include <hurd/store.h>

#include "dev.h"

/* These functions deal with the buffer used for doing non-block-aligned I/O. */

static inline int
dev_buf_is_active (struct dev *dev)
{
  return dev->buf_offs >= 0;
}

/* Invalidate DEV's buffer, writing it to disk if necessary.  */
static error_t
dev_buf_discard (struct dev *dev)
{
  if (dev_buf_is_active (dev))
    {
      if (dev->buf_dirty)
	{
	  size_t amount;
	  struct store *store = dev->store;
	  error_t err =
	    store_write (store, dev->buf_offs >> store->log2_block_size,
			 dev->buf, store->block_size, &amount);
	  if (amount < store->block_size)
	    err = EIO;
	  if (err)
	    return err;
	  dev->buf_dirty = 0;
	}
      dev->buf_offs = -1;
    }
  return 0;
}

/* Make DEV's buffer active, reading the block from DEV's store which
   contains OFFS.  */
static error_t
dev_buf_fill (struct dev *dev, off_t offs)
{
  error_t err;
  unsigned block_mask = dev->block_mask;
  char *buf = dev->buf;
  struct store *store = dev->store;
  size_t buf_len = store->block_size;

  if (dev_buf_is_active (dev))
    if ((dev->buf_offs & ~block_mask) == (offs & ~block_mask))
      return 0;			/* Correct block alredy in buffer.  */
    else
      {
	err = dev_buf_discard (dev);
	if (err)
	  return err;
      }

  err = store_read (store, offs >> store->log2_block_size, store->block_size,
		    &buf, &buf_len);
  if (err)
    return err;

  if (buf != dev->buf)
    {
      vm_deallocate (mach_task_self (),
		     (vm_address_t)dev->buf, store->block_size);
      dev->buf = buf;
    }

  dev->buf_offs = offs & ~block_mask;

  return 0;
}

/* Do an in-buffer partial-block I/O operation.  */
static error_t
dev_buf_rw (struct dev *dev, size_t buf_offs, size_t *io_offs, size_t *len,
	    inline error_t (*const buf_rw) (size_t buf_offs,
					    size_t io_offs, size_t len))
{
  size_t block_size = dev->store->block_size;

  assert (dev_buf_is_active (dev));

  if (buf_offs + *len >= block_size)
    /* Only part of BUF lies within the buffer (or everything up
       to the end of the block, in which case we want to flush
       the buffer anyway).  */
    {
      size_t buf_len = block_size - buf_offs;
      error_t err = (*buf_rw) (buf_offs, *io_offs, buf_len);
      if (err)
	return err;
      *io_offs += buf_len;
      *len -= buf_len;
      return dev_buf_discard (dev);
    }
  else
    /* All I/O is within the block.  */
    {
      error_t err = (*buf_rw) (buf_offs, *io_offs, *len);
      if (err)
	return err;
      *io_offs += *len;
      *len = 0;
      return 0;
    }
}

/* Returns a pointer to a new device structure in DEV for the kernel device
   NAME, with the given FLAGS.  If BLOCK_SIZE is non-zero, it should be the
   desired block size, and must be a multiple of the device block size.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
dev_open (struct store_parsed *name, int flags, struct dev **dev)
{
  error_t err;
  struct dev *new = malloc (sizeof (struct dev));

  if (! new)
    return ENOMEM;

  err = store_parsed_open (name, flags, 0, &new->store);
  if (err)
    {
      free (new);
      return err;
    }

#if 0 /* valloc doesn't work */
  new->buf = valloc (new->store->block_size);
  if (new->buf == 0)
#else
  if (vm_allocate (mach_task_self (),
		   (vm_address_t *)&new->buf, new->store->block_size, 1))
#endif
    {
      store_free (new->store);
      free (new);
      return ENOMEM;
    }

  new->buf_offs = -1;
  rwlock_init (&new->io_lock);
  new->owner = 0;
  new->block_mask = (1 << new->store->log2_block_size) - 1;
  new->pager = 0;
  mutex_init (&new->pager_lock);
  *dev = new;

  return 0;
}

/* Free DEV and any resources it consumes.  */
void 
dev_close (struct dev *dev)
{
  if (dev->pager != NULL)
    pager_shutdown (dev->pager);

  dev_buf_discard (dev);

#if 0
  free (dev->buf);
#else
  vm_deallocate (mach_task_self (),
		 (vm_address_t)dev->buf, dev->store->block_size);
#endif

  store_free (dev->store);

  free (dev);
}

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t
dev_sync(struct dev *dev, int wait)
{
  error_t err;

  /* Sync any paged backing store.  */
  if (dev->pager != NULL)
    pager_sync (dev->pager, wait);

  rwlock_writer_lock (&dev->io_lock);
  err = dev_buf_discard (dev);
  rwlock_writer_unlock (&dev->io_lock);

  return err;
}

/* Takes care of buffering I/O to/from DEV for a transfer at position OFFS,
   length LEN; the amount of I/O sucessfully done is returned in AMOUNT.
   BUF_RW is called to do I/O that's entirely inside DEV's internal buffer,
   and RAW_RW to do I/O directly to DEV's store.  */
static inline error_t
buffered_rw (struct dev *dev, off_t offs, size_t len, size_t *amount,
	     inline error_t (* const buf_rw) (size_t buf_offs,
					      size_t io_offs, size_t len),
	     inline error_t (* const raw_rw) (off_t offs,
					      size_t io_offs, size_t len,
					      size_t *amount))
{
  error_t err = 0;
  unsigned block_mask = dev->block_mask;
  unsigned block_size = dev->store->block_size;
  size_t io_offs = 0;		/* Offset within this I/O operation.  */
  unsigned block_offs = offs & block_mask; /* Offset within a block.  */

  rwlock_writer_lock (&dev->io_lock);

  if (block_offs != 0)
    /* The start of the I/O isn't block aligned.  */
    {
      err = dev_buf_fill (dev, offs);
      if (! err)
	err = dev_buf_rw (dev, block_offs, &io_offs, &len, buf_rw);
    }

  if (!err && len > 0)
    /* Now the I/O should be block aligned.  */
    {
      if (len >= block_size)
	{
	  size_t amount;
	  err = dev_buf_discard (dev);
	  if (! err)
	    err =
	      (*raw_rw) (offs + io_offs, io_offs, len & ~block_mask, &amount);
	  if (! err)
	    {
	      io_offs += amount;
	      len -= amount;
	    }
	}
      if (len > 0 && len < block_size)
	/* All full blocks were written successfully, so write
	   the tail end into the buffer.  */
	{
	  err = dev_buf_fill (dev, offs + io_offs);
	  if (! err)
	    err = dev_buf_rw (dev, 0, &io_offs, &len, buf_rw);
	}
    }

  if (! err)
    *amount = io_offs;

  rwlock_writer_unlock (&dev->io_lock);

  return err;
}

/* Takes care of buffering I/O to/from DEV for a transfer at position OFFS,
   length LEN, and direction DIR.  BUF_RW is called to do I/O to/from data
   buffered in DEV, and RAW_RW to do I/O directly to DEV's store.  */
static inline error_t
dev_rw (struct dev *dev, off_t offs, size_t len, size_t *amount,
	inline error_t (* const buf_rw) (size_t buf_offs,
					 size_t io_offs, size_t len),
	inline error_t (* const raw_rw) (off_t offs,
					 size_t io_offs, size_t len,
					 size_t *amount))
{
  error_t err;
  unsigned block_mask = dev->block_mask;

  if (offs < 0)
    return EINVAL;
  else if (offs > dev->store->size)
    return EIO;
  else if (offs + len > dev->store->size)
    len = dev->store->size - offs;

  rwlock_reader_lock (&dev->io_lock);
  if (dev_buf_is_active (dev)
      || (offs & block_mask) != 0 || (len & block_mask) != 0)
    /* Some non-aligned I/O has been done, or is needed, so we need to deal
       with DEV's buffer, which means getting an exclusive lock.  */
    {
      /* Aquire a writer lock instead of a reader lock.  Note that other
	 writers may have aquired the lock by the time we get it.  */
      rwlock_reader_unlock (&dev->io_lock);
      err = buffered_rw (dev, offs, len, amount, buf_rw, raw_rw);
    }
  else
    /* Only block-aligned I/O is being done, so things are easy.  */
    {
      err = (*raw_rw) (offs, 0, len, amount);
      rwlock_reader_unlock (&dev->io_lock);
    }

  return err;
}

/* Write LEN bytes from BUF to DEV, returning the amount actually written in
   AMOUNT.  If successful, 0 is returned, otherwise an error code is
   returned.  */
error_t
dev_write (struct dev *dev, off_t offs, char *buf, size_t len,
	   size_t *amount)
{
  error_t buf_write (size_t buf_offs, size_t io_offs, size_t len)
    {
      bcopy (buf + io_offs, dev->buf + buf_offs, len);
      dev->buf_dirty = 1;
      return 0;
    }
  error_t raw_write (off_t offs, size_t io_offs, size_t len, size_t *amount)
    {
      struct store *store = dev->store;
      return
	store_write (store, offs >> store->log2_block_size,
		     buf + io_offs, len, amount);
    }

  return dev_rw (dev, offs, len, amount, buf_write, raw_write);
}

/* Read up to WHOLE_AMOUNT bytes from DEV, returned in BUF and LEN in the
   with the usual mach memory result semantics.  If successful, 0 is
   returned, otherwise an error code is returned.  */
error_t
dev_read (struct dev *dev, off_t offs, size_t whole_amount,
	  char **buf, size_t *len)
{
  error_t err;
  int allocated_buf = 0;
  error_t ensure_buf ()
    {
      error_t err;
      if (*len < whole_amount)
	{
	  err = vm_allocate (mach_task_self (),
			     (vm_address_t *)buf, whole_amount, 1);
	  if (! err)
	    allocated_buf = 1;
	}
      else
	err = 0;
      return err;
    }
  error_t buf_read (size_t buf_offs, size_t io_offs, size_t len)
    {
      error_t err = ensure_buf ();
      if (! err)
	bcopy (dev->buf + buf_offs, *buf + io_offs, len);
      return err;
    }
  error_t raw_read (off_t offs, size_t io_offs, size_t len, size_t *amount)
    {
      struct store *store = dev->store;
      off_t addr = offs >> store->log2_block_size;
      if (len == whole_amount)
	/* Just return whatever the device does.  */
	return store_read (store, addr, len, buf, amount);
      else
	/* This read is returning less than the whole request, so we allocate
	   a buffer big enough to hold everything, in case we have to
	   coalesce multiple reads into a single return buffer.  */
	{
	  error_t err = ensure_buf ();
	  if (! err)
	    {
	      char *_req_buf = *buf + io_offs, *req_buf = _req_buf;
	      size_t req_len = len;
	      err = store_read (store, addr, len, &req_buf, &req_len);
	      if (! err)
		{
		  if (req_buf != _req_buf)
		    /* Copy from wherever the read put it. */
		    {
		      bcopy (req_buf, _req_buf, req_len);
		      vm_deallocate (mach_task_self (),
				     (vm_address_t)req_buf, req_len);
		    }
		  *amount = req_len;
		}
	    }
	  return err;
	}
    }

  err = dev_rw (dev, offs, whole_amount, len, buf_read, raw_read);
  if (err && allocated_buf)
    vm_deallocate (mach_task_self (), (vm_address_t)*buf, whole_amount);

  return err;
}     
