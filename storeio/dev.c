/* store `device' I/O

   Copyright (C) 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2008
     Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <assert-backtrace.h>
#include <string.h>
#include <hurd/pager.h>
#include <hurd/store.h>
#include <sys/mman.h>

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
	  if (!err && amount < store->block_size)
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
  void *buf = dev->buf;
  struct store *store = dev->store;
  size_t buf_len = store->block_size;

  if (dev_buf_is_active (dev))
    {
      if ((dev->buf_offs & ~block_mask) == (offs & ~block_mask))
	return 0;			/* Correct block alredy in buffer.  */
      else
	{
	  err = dev_buf_discard (dev);
	  if (err)
	    return err;
	}
    }

  err = store_read (store, offs >> store->log2_block_size, store->block_size,
		    &buf, &buf_len);
  if (err)
    return err;

  if (buf != dev->buf)
    {
      munmap (dev->buf, store->block_size);
      dev->buf = buf;
    }

  dev->buf_offs = offs & ~block_mask;

  return 0;
}

/* Do an in-buffer partial-block I/O operation.  */
static error_t
dev_buf_rw (struct dev *dev, size_t buf_offs, size_t *io_offs, size_t *len,
	    error_t (*const buf_rw) (size_t buf_offs,
				     size_t io_offs, size_t len))
{
  size_t block_size = dev->store->block_size;

  assert_backtrace (dev_buf_is_active (dev));

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

/* Called with DEV->lock held.  Try to open the store underlying DEV.  */
error_t
dev_open (struct dev *dev)
{
  error_t err;
  const int flags = ((dev->readonly ? STORE_READONLY : 0)
		     | (dev->no_fileio ? STORE_NO_FILEIO : 0));

  assert_backtrace (dev->store == 0);

  if (dev->store_name == 0)
    {
      /* This means we had no store arguments.
	 We are to operate on our underlying node. */
      err = store_create (storeio_fsys->underlying, flags, 0, &dev->store);
    }
  else
    /* Open based on the previously parsed store arguments.  */
    err = store_parsed_open (dev->store_name, flags, &dev->store);
  if (err)
    return err;

  /* Inactivate the store, it will be activated at first access.
     We ignore possible EINVAL here   .  XXX Pass STORE_INACTIVE to
     store_create/store_parsed_open instead when libstore is fixed
     to support this.  */
  store_set_flags (dev->store, STORE_INACTIVE);

  if (! dev->store->block_size)
    dev->buf = NULL;
  else
    dev->buf = mmap (0, dev->store->block_size, PROT_READ|PROT_WRITE,
		     MAP_ANON, 0, 0);
  if (dev->buf == MAP_FAILED)
    {
      store_free (dev->store);
      dev->store = 0;
      return ENOMEM;
    }

  if (!dev->inhibit_cache)
    {
      dev->buf_offs = -1;
      pthread_rwlock_init (&dev->io_lock, NULL);
      dev->block_mask = (1 << dev->store->log2_block_size) - 1;
      dev->pager = 0;
      pthread_mutex_init (&dev->pager_lock, NULL);
    }

  return 0;
}

/* Shut down the store underlying DEV and free any resources it consumes.
   DEV itself remains intact so that dev_open can be called again.
   This should be called with DEV->lock held.  */
void
dev_close (struct dev *dev)
{
  assert_backtrace (dev->store);

  if (!dev->inhibit_cache)
    {
      if (dev->pager != NULL)
	pager_shutdown (dev->pager);

      dev_buf_discard (dev);

      munmap (dev->buf, dev->store->block_size);
    }

  store_free (dev->store);
  dev->store = 0;
}

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t
dev_sync(struct dev *dev, int wait)
{
  error_t err;

  if (dev->inhibit_cache)
    return 0;

  /* Sync any paged backing store.  */
  if (dev->pager != NULL)
    pager_sync (dev->pager, wait);

  pthread_rwlock_wrlock (&dev->io_lock);
  err = dev_buf_discard (dev);
  pthread_rwlock_unlock (&dev->io_lock);

  return err;
}

/* Takes care of buffering I/O to/from DEV for a transfer at position OFFS,
   length LEN; the amount of I/O successfully done is returned in AMOUNT.
   BUF_RW is called to do I/O that's entirely inside DEV's internal buffer,
   and RAW_RW to do I/O directly to DEV's store.  */
static inline error_t
buffered_rw (struct dev *dev, off_t offs, size_t len, size_t *amount,
	     error_t (* const buf_rw) (size_t buf_offs,
				       size_t io_offs, size_t len),
	     error_t (* const raw_rw) (off_t offs,
				       size_t io_offs, size_t len,
				       size_t *amount))
{
  error_t err = 0;
  unsigned block_mask = dev->block_mask;
  unsigned block_size = dev->store->block_size;
  size_t io_offs = 0;		/* Offset within this I/O operation.  */
  unsigned block_offs = offs & block_mask; /* Offset within a block.  */

  pthread_rwlock_wrlock (&dev->io_lock);

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

  pthread_rwlock_unlock (&dev->io_lock);

  return err;
}

/* Takes care of buffering I/O to/from DEV for a transfer at position OFFS,
   length LEN, and direction DIR.  BUF_RW is called to do I/O to/from data
   buffered in DEV, and RAW_RW to do I/O directly to DEV's store.  */
static inline error_t
dev_rw (struct dev *dev, off_t offs, size_t len, size_t *amount,
	error_t (* const buf_rw) (size_t buf_offs,
				  size_t io_offs, size_t len),
	error_t (* const raw_rw) (off_t offs,
				  size_t io_offs, size_t len,
				  size_t *amount))
{
  error_t err;
  unsigned block_mask = dev->block_mask;

  if (offs < 0 || offs > dev->store->size)
    return EINVAL;
  else if (offs + len > dev->store->size)
    len = dev->store->size - offs;

  pthread_rwlock_rdlock (&dev->io_lock);
  if (dev_buf_is_active (dev)
      || (offs & block_mask) != 0 || (len & block_mask) != 0)
    /* Some non-aligned I/O has been done, or is needed, so we need to deal
       with DEV's buffer, which means getting an exclusive lock.  */
    {
      /* Acquire a writer lock instead of a reader lock.  Note that other
	 writers may have acquired the lock by the time we get it.  */
      pthread_rwlock_unlock (&dev->io_lock);
      err = buffered_rw (dev, offs, len, amount, buf_rw, raw_rw);
    }
  else
    /* Only block-aligned I/O is being done, so things are easy.  */
    {
      err = (*raw_rw) (offs, 0, len, amount);
      pthread_rwlock_unlock (&dev->io_lock);
    }

  return err;
}

/* Write LEN bytes from BUF to DEV, returning the amount actually written in
   AMOUNT.  If successful, 0 is returned, otherwise an error code is
   returned.  */
error_t
dev_write (struct dev *dev, off_t offs, void *buf, size_t len,
	   size_t *amount)
{
  error_t buf_write (size_t buf_offs, size_t io_offs, size_t len)
    {
      memcpy (dev->buf + buf_offs, buf + io_offs, len);
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

  if (dev->inhibit_cache)
    {
      /* Under --no-cache, we permit only whole-block writes.
	 Note that in this case we handle non-power-of-two block sizes.  */

      struct store *store = dev->store;

      if (store->block_size == 0)
	/* We don't know the block size, so let the device enforce it.  */
	return store_write (dev->store, offs, buf, len, amount);

      if ((offs & (store->block_size - 1)) != 0
	  || (len & (store->block_size - 1)) != 0)
	/* Not whole blocks.  No can do.  */
	return EINVAL;	/* EIO? */

      /* Do a direct write to the store.  */
      return store_write (dev->store, offs << store->log2_block_size,
			  buf, len, amount);
    }

  return dev_rw (dev, offs, len, amount, buf_write, raw_write);
}

/* Read up to WHOLE_AMOUNT bytes from DEV, returned in BUF and LEN in the
   with the usual mach memory result semantics.  If successful, 0 is
   returned, otherwise an error code is returned.  */
error_t
dev_read (struct dev *dev, off_t offs, size_t whole_amount,
	  void **buf, size_t *len)
{
  error_t err;
  int allocated_buf = 0;
  error_t ensure_buf ()
    {
      if (*len < whole_amount)
	{
	  void *new = mmap (0, whole_amount, PROT_READ|PROT_WRITE,
			    MAP_ANON, 0, 0);
	  if (new == (void *) -1)
	    return errno;
	  *buf = new;
	  allocated_buf = 1;
	}
      return 0;
    }
  error_t buf_read (size_t buf_offs, size_t io_offs, size_t len)
    {
      error_t err = ensure_buf ();
      if (! err)
	memcpy (*buf + io_offs, dev->buf + buf_offs, len);
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
	      void *_req_buf = *buf + io_offs, *req_buf = _req_buf;
	      size_t req_len = len;
	      err = store_read (store, addr, len, &req_buf, &req_len);
	      if (! err)
		{
		  if (req_buf != _req_buf)
		    /* Copy from wherever the read put it. */
		    {
		      memcpy (_req_buf, req_buf, req_len);
		      munmap (req_buf, req_len);
		    }
		  *amount = req_len;
		}
	    }
	  return err;
	}
    }

  if (dev->store->size > 0 && offs == dev->store->size)
    {
      /* Reading end of file.  */
      *len = 0;
      return 0;
    }

  if (dev->inhibit_cache)
    {
      /* Under --no-cache, we permit only whole-block reads.
	 Note that in this case we handle non-power-of-two block sizes.
	 We could, that is, but libstore won't have it (see libstore/make.c).
	 If the device does not report a block size, we let any attempt
	 through on the assumption the device will enforce its own limits.  */

      struct store *store = dev->store;

      if (store->block_size == 0)
	/* We don't know the block size, so let the device enforce it.  */
	return store_read (dev->store, offs, whole_amount, buf, len);

      if ((offs & (store->block_size - 1)) != 0
	  || (whole_amount & (store->block_size - 1)) != 0)
	/* Not whole blocks.  No can do.  */
	return EINVAL;

      /* Do a direct read from the store.  */
      return store_read (dev->store, offs << store->log2_block_size,
			 whole_amount, buf, len);
    }

  err = dev_rw (dev, offs, whole_amount, len, buf_read, raw_read);
  if (err && allocated_buf)
    munmap (*buf, whole_amount);

  return err;
}
