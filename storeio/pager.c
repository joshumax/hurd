/* Paging interface for storeio devices

   Copyright (C) 1995,96,97,99,2002 Free Software Foundation, Inc.

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
#include <hurd/pager.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "dev.h"

/* ---------------------------------------------------------------- */
/* Pager library callbacks; see <hurd/pager.h> for more info.  */

struct user_pager_info
{
  struct dev *dev;
};

/* For pager PAGER, read NPAGES pages from page PAGE.  Set *BUF to be the
   address of the page, and set *WRITE_LOCK if the page must be provided
   read-only.  The only permissible error returns are EIO, EDQUOT, and
   ENOSPC. */
void
store_read_pages (struct pager *pager, struct user_pager_info *upi,
        	  off_t start, off_t npages)
{
  error_t err;
  void *buf;
  int writelock;
  size_t read = 0;			/* bytes actually read */
  off_t page = start * vm_page_size;
  int want = npages * vm_page_size;	/* bytes we want to read */
  struct dev *dev = upi->dev;
  struct store *store = dev->store;

  if (page + want > store->size)
    /* Read a partial page if necessary to avoid reading off the end.  */
    want = store->size - page;

  err = dev_read (dev, page, want, &buf, &read);

  if (!err && want < round_page (want))
    /* Zero anything we didn't read.  Allocation only happens in page-size
       multiples, so we know we can write there.  */
    memset ((char *)buf + want, '\0', round_page (want) - want);

  writelock = (store->flags & STORE_READONLY);

  if (err || read < want)
    pager_data_read_error (pager, start, npages, EIO);
  else
    pager_data_supply (pager, 0, writelock, start, npages, buf, 1);
}

/* For pager PAGER, synchronously write NPAGES pages from BUF to page PAGE.
   In DEALLOC is TRUE, vm_deallocate (or equivalent) BUF.  The only
   permissible error returns are EIO, EDQUOT, and ENOSPC. */
void
store_write_pages (struct pager *pager, struct user_pager_info *upi,
		   off_t start, off_t npages, void *buf, int dealloc)
{
  struct dev *dev = upi->dev;
  struct store *store = dev->store;
  off_t page = start * vm_page_size;

  if (store->flags & STORE_READONLY)
    return pager_data_write_error (pager, start, npages, EROFS);
  else
    {
      error_t err;
      size_t written;
      int want = npages * vm_page_size;

      if (page + want > store->size)
	/* Write a partial page if necessary to avoid reading off the end.  */
	want = store->size - page;

      err = dev_write (dev, page, (char *)buf, want, &written);

      if (dealloc)
	munmap (buf, npages * vm_page_size);

      if (err || written < want)
	return pager_data_write_error (pager, start, npages, EIO);
    }
}

/* A page should be made writable. */
void
store_unlock_pages (struct pager *pager,
		   struct user_pager_info *upi,
		   off_t start, off_t npages)
{
  struct dev *dev = upi->dev;

  if (dev->store->flags & STORE_READONLY)
    pager_data_unlock_error (pager, start, npages, EROFS);
  else
    pager_data_unlock (pager, start, npages);
}

/* The user must define this function.  It should report back (in
   *OFFSET and *SIZE the minimum valid address the pager will accept
   and the size of the object.   */
void
store_report_extent (struct user_pager_info *upi,
		     off_t *offset, off_t *size)
{
  *offset = 0;
  *size = upi->dev->store->size;
}

/* This is called when a pager is being deallocated after all extant send
   rights have been destroyed.  */
void
store_clear_user_data (struct user_pager_info *upi)
{
  struct dev *dev = upi->dev;
  mutex_lock (&dev->pager_lock);
  dev->pager = 0;
  mutex_unlock (&dev->pager_lock);
}

static struct port_bucket *pager_port_bucket = 0;

/* A top-level function for the paging thread that just services paging
   requests.  */
static void
service_paging_requests (any_t arg)
{
  for (;;)
    ports_manage_port_operations_multithread (pager_port_bucket,
					      pager_demuxer,
					      1000 * 30, 1000 * 60 * 5, 0);
}

/* Initialize paging for this device.  */
static void
init_dev_paging ()
{
  if (! pager_port_bucket)
    {
      static struct mutex pager_global_lock = MUTEX_INITIALIZER;

      mutex_lock (&pager_global_lock);
      if (pager_port_bucket == NULL)
	{
	  pager_port_bucket = ports_create_bucket ();

	  /* Make a thread to service paging requests.  */
	  cthread_detach (cthread_fork ((cthread_fn_t)service_paging_requests,
					(any_t)0));
	}
      mutex_unlock (&pager_global_lock);
    }
}

struct pager_ops store_ops =
  {
    .read = &store_read_pages,
    .write = &store_write_pages,
    .unlock = &store_unlock_pages,
    .report_extent = &store_report_extent,
    .clear_user_data = &store_clear_user_data,
    .dropweak = NULL
  };

/* Try to stop all paging activity on DEV, returning true if we were
   successful.  If NOSYNC is true, then we won't write back any (kernel)
   cached pages to the device.  */
int
dev_stop_paging (struct dev *dev, int nosync)
{
  size_t num_pagers = (pager_port_bucket ?
		       ports_count_bucket (pager_port_bucket) : 0);

  if (num_pagers > 0 && !nosync)
    {
      error_t block_cache (void *arg)
	{
	  struct pager *p = arg;
	  pager_change_attributes (p, 0, MEMORY_OBJECT_COPY_DELAY, 1);
	  return 0;
	}
      error_t enable_cache (void *arg)
	{
	  struct pager *p = arg;
	  pager_change_attributes (p, 1, MEMORY_OBJECT_COPY_DELAY, 0);
	  return 0;
	}

      /* Loop through the pagers and turn off caching one by one,
	 synchronously.  That should cause termination of each pager. */
      ports_bucket_iterate (pager_port_bucket, block_cache);

      /* Give it a second; the kernel doesn't actually shutdown
	 immediately.  XXX */
      sleep (1);

      num_pagers = ports_count_bucket (pager_port_bucket);
      if (num_pagers > 0)
	/* Darn, there are actual honest users.  Turn caching back on,
	   and return failure. */
	ports_bucket_iterate (pager_port_bucket, enable_cache);
    }

  return num_pagers == 0;
}

/* Returns in MEMOBJ the port for a memory object backed by the storage on
   DEV.  Returns 0 or the error code if an error occurred.  */
error_t
dev_get_memory_object (struct dev *dev, vm_prot_t prot, memory_object_t *memobj)
{
  error_t err = store_map (dev->store, prot, memobj);

  if (err == EOPNOTSUPP && !dev->inhibit_cache)
    {
      int created = 0;

      init_dev_paging ();

      mutex_lock (&dev->pager_lock);

      if (dev->pager == NULL)
	{
	  size_t upi_size = sizeof (struct user_pager_info);
	  dev->pager =
	    pager_create (&store_ops, upi_size, pager_port_bucket,
			  TRUE, MEMORY_OBJECT_COPY_DELAY);
	  if (dev->pager == NULL)
	    {
	      mutex_unlock (&dev->pager_lock);
	      return errno;
	    }
	  created = 1;
	  pager_get_upi (dev->pager)->dev = dev;
	}

      *memobj = pager_get_port (dev->pager);

      if (*memobj == MACH_PORT_NULL)
	/* Pager is currently being destroyed, try again.  */
	{
	  dev->pager = 0;
	  mutex_unlock (&dev->pager_lock);
	  return dev_get_memory_object (dev, prot, memobj);
	}
      else
	err =
	  mach_port_insert_right (mach_task_self (),
				  *memobj, *memobj, MACH_MSG_TYPE_MAKE_SEND);

      if (created)
	ports_port_deref (dev->pager);

      mutex_unlock (&dev->pager_lock);
    }

  return err;
}
