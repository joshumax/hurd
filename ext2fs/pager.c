/* Pager for ext2fs

   Copyright (C) 1994,95,96,97,98,99,2000,02 Free Software Foundation, Inc.

   Converted for ext2fs by Miles Bader <miles@gnu.org>

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

#include <string.h>
#include <errno.h>
#include <hurd/store.h>
#include "ext2fs.h"

/* A ports bucket to hold pager ports.  */
struct port_bucket *pager_bucket;

/* Mapped image of the disk.  */
void *disk_image;

spin_lock_t node_to_page_lock = SPIN_LOCK_INITIALIZER;

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

#define STATS

#ifdef STATS
struct ext2fs_pager_stats
{
  spin_lock_t lock;

  unsigned long disk_pageins;
  unsigned long disk_pageouts;

  unsigned long file_pageins;
  unsigned long file_pagein_reads; /* Device reads done by file pagein */
  unsigned long file_pagein_freed_bufs;	/* Discarded pages */
  unsigned long file_pagein_alloced_bufs; /* Allocated pages */

  unsigned long file_pageouts;

  unsigned long file_page_unlocks;
  unsigned long file_grows;
};

static struct ext2fs_pager_stats ext2s_pager_stats;

#define STAT_INC(field)							      \
do { spin_lock (&ext2s_pager_stats.lock);				      \
     ext2s_pager_stats.field++;						      \
     spin_unlock (&ext2s_pager_stats.lock); } while (0)

#else /* !STATS */
#define STAT_INC(field) /* nop */0
#endif /* STATS */

#define FREE_PAGE_BUFS 24

/* Returns a single page page-aligned buffer.  */
static void *
get_page_buf ()
{
  static struct mutex free_page_bufs_lock = MUTEX_INITIALIZER;
  static void *free_page_bufs;
  static int num_free_page_bufs;
  void *buf;

  mutex_lock (&free_page_bufs_lock);
  if (num_free_page_bufs > 0)
    {
      buf = free_page_bufs;
      num_free_page_bufs --;
      if (num_free_page_bufs > 0)
	free_page_bufs += vm_page_size;
#ifndef NDEBUG
      else
	free_page_bufs = 0;
#endif /* ! NDEBUG */
    }
  else
    {
      assert (free_page_bufs == 0);
      buf = mmap (0, vm_page_size * FREE_PAGE_BUFS,
		  PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (buf == MAP_FAILED)
	buf = 0;
      else
	{
	  free_page_bufs = buf + vm_page_size;
	  num_free_page_bufs = FREE_PAGE_BUFS - 1;
	}
    }

  mutex_unlock (&free_page_bufs_lock);
  return buf;
}

/* Frees a block returned by get_page_buf.  */
static inline void
free_page_buf (void *buf)
{
  munmap (buf, vm_page_size);
}

/* Find the location on disk of page OFFSET in NODE.  Return the disk block
   in BLOCK (if unallocated, then return 0).  If *LOCK is 0, then a reader
   lock is acquired on NODE's ALLOC_LOCK before doing anything, and left
   locked after the return -- even if an error is returned.  0 is returned
   on success otherwise an error code.  */
static error_t
find_block (struct node *node, vm_offset_t offset,
	    block_t *block, struct rwlock **lock)
{
  error_t err;

  if (!*lock)
    {
      *lock = &node->dn->alloc_lock;
      rwlock_reader_lock (*lock);
    }

  if (offset + block_size > node->allocsize)
    return EIO;

  err = ext2_getblk (node, offset >> log2_block_size, 0, block);
  if (err == EINVAL)
    /* Don't barf yet if the node is unallocated.  */
    {
      *block = 0;
      err = 0;
    }

  return err;
}

/* Read one page for the pager backing NODE at offset PAGE, into BUF.  This
   may need to read several filesystem blocks to satisfy one page, and tries
   to consolidate the i/o if possible.  */
static error_t
file_pager_read_page (struct node *node, vm_offset_t page,
		      void **buf, int *writelock)
{
  error_t err;
  int offs = 0;
  int partial = 0;		/* A page truncated by the EOF.  */
  struct rwlock *lock = NULL;
  int left = vm_page_size;
  block_t pending_blocks = 0;
  int num_pending_blocks = 0;

  /* Read the NUM_PENDING_BLOCKS blocks in PENDING_BLOCKS, into the buffer
     pointed to by BUF (allocating it if necessary) at offset OFFS.  OFFS in
     adjusted by the amount read, and NUM_PENDING_BLOCKS is zeroed.  Any read
     error is returned.  */
  error_t do_pending_reads ()
    {
      if (num_pending_blocks > 0)
	{
	  block_t dev_block = pending_blocks << log2_dev_blocks_per_fs_block;
	  size_t amount = num_pending_blocks << log2_block_size;
	  /* The buffer we try to read into; on the first read, we pass in a
	     size of zero, so that the read is guaranteed to allocate a new
	     buffer, otherwise, we try to read directly into the tail of the
	     buffer we've already got.  */
	  void *new_buf = *buf + offs;
	  size_t new_len = offs == 0 ? 0 : vm_page_size - offs;

	  STAT_INC (file_pagein_reads);

	  err = store_read (store, dev_block, amount, &new_buf, &new_len);
	  if (err)
	    return err;
	  else if (amount != new_len)
	    return EIO;

	  if (new_buf != *buf + offs)
	    {
	      /* The read went into a different buffer than the one we
                 passed. */
	      if (offs == 0)
		/* First read, make the returned page be our buffer.  */
		*buf = new_buf;
	      else
		/* We've already got some buffer, so copy into it.  */
		{
		  bcopy (new_buf, *buf + offs, new_len);
		  free_page_buf (new_buf); /* Return NEW_BUF to our pool.  */
		  STAT_INC (file_pagein_freed_bufs);
		}
	    }

	  offs += new_len;
	  num_pending_blocks = 0;
	}

      return 0;
    }

  STAT_INC (file_pageins);

  *writelock = 0;

  if (page >= node->allocsize)
    {
      err = EIO;
      left = 0;
    }
  else if (page + left > node->allocsize)
    {
      left = node->allocsize - page;
      partial = 1;
    }

  while (left > 0)
    {
      block_t block;

      err = find_block (node, page, &block, &lock);
      if (err)
	break;

      if (block != pending_blocks + num_pending_blocks)
	{
	  err = do_pending_reads ();
	  if (err)
	    break;
	  pending_blocks = block;
	}

      if (block == 0)
	/* Reading unallocated block, just make a zero-filled one.  */
	{
	  *writelock = 1;
	  if (offs == 0)
	    /* No page allocated to read into yet.  */
	    {
	      *buf = get_page_buf ();
	      if (! *buf)
		break;
	      STAT_INC (file_pagein_alloced_bufs);
	    }
	  bzero (*buf + offs, block_size);
	  offs += block_size;
	}
      else
	num_pending_blocks++;

      page += block_size;
      left -= block_size;
    }

  if (!err && num_pending_blocks > 0)
    err = do_pending_reads();

  if (!err && partial && !*writelock)
    node->dn->last_page_partially_writable = 1;

  if (lock)
    rwlock_reader_unlock (lock);

  return err;
}

struct pending_blocks
{
  /* The block number of the first of the blocks.  */
  block_t block;
  /* How many blocks we have.  */
  off_t num;
  /* A (page-aligned) buffer pointing to the data we're dealing with.  */
  void *buf;
  /* And an offset into BUF.  */
  int offs;
};

/* Write the any pending blocks in PB.  */
static error_t
pending_blocks_write (struct pending_blocks *pb)
{
  if (pb->num > 0)
    {
      error_t err;
      block_t dev_block = pb->block << log2_dev_blocks_per_fs_block;
      size_t length = pb->num << log2_block_size, amount;

      ext2_debug ("writing block %u[%Ld]", pb->block, pb->num);

      if (pb->offs > 0)
	/* Put what we're going to write into a page-aligned buffer.  */
	{
	  void *page_buf = get_page_buf ();
	  bcopy (pb->buf + pb->offs, (void *)page_buf, length);
	  err = store_write (store, dev_block, page_buf, length, &amount);
	  free_page_buf (page_buf);
	}
      else
	err = store_write (store, dev_block, pb->buf, length, &amount);
      if (err)
	return err;
      else if (amount != length)
	return EIO;

      pb->offs += length;
      pb->num = 0;
    }

  return 0;
}

static void
pending_blocks_init (struct pending_blocks *pb, void *buf)
{
  pb->buf = buf;
  pb->block = 0;
  pb->num = 0;
  pb->offs = 0;
}

/* Skip writing the next block in PB's buffer (writing out any previous
   blocks if necessary).  */
static error_t
pending_blocks_skip (struct pending_blocks *pb)
{
  error_t err = pending_blocks_write (pb);
  pb->offs += block_size;
  return err;
}

/* Add the disk block BLOCK to the list of destination disk blocks pending in
   PB.  */
static error_t
pending_blocks_add (struct pending_blocks *pb, block_t block)
{
  if (block != pb->block + pb->num)
    {
      error_t err = pending_blocks_write (pb);
      if (err)
	return err;
      pb->block = block;
    }
  pb->num++;
  return 0;
}

/* Write one page for the pager backing NODE, at offset PAGE, into BUF.  This
   may need to write several filesystem blocks to satisfy one page, and tries
   to consolidate the i/o if possible.  */
static error_t
file_pager_write_page (struct node *node, vm_offset_t offset, void *buf)
{
  error_t err = 0;
  struct pending_blocks pb;
  struct rwlock *lock = &node->dn->alloc_lock;
  block_t block;
  int left = vm_page_size;

  pending_blocks_init (&pb, buf);

  /* Holding NODE->dn->alloc_lock effectively locks NODE->allocsize,
     at least for the cases we care about: pager_unlock_page,
     diskfs_grow and diskfs_truncate.  */
  rwlock_reader_lock (&node->dn->alloc_lock);

  if (offset >= node->allocsize)
    left = 0;
  else if (offset + left > node->allocsize)
    left = node->allocsize - offset;

  ext2_debug ("writing inode %Ld page %lu[%d]", node->cache_id, offset, left);

  STAT_INC (file_pageouts);

  while (left > 0)
    {
      err = find_block (node, offset, &block, &lock);
      if (err)
	break;
      assert (block);
      pending_blocks_add (&pb, block);
      offset += block_size;
      left -= block_size;
    }

  if (!err)
    pending_blocks_write (&pb);

  rwlock_reader_unlock (&node->dn->alloc_lock);

  return err;
}

static error_t
disk_pager_read_page (vm_offset_t page, void **buf, int *writelock)
{
  error_t err;
  size_t length = vm_page_size, read = 0;
  vm_size_t dev_end = store->size;

  if (page + vm_page_size > dev_end)
    length = dev_end - page;

  err = store_read (store, page >> store->log2_block_size, length, buf, &read);
  if (read != length)
    return EIO;
  if (!err && length != vm_page_size)
    bzero ((void *)(*buf + length), vm_page_size - length);

  *writelock = 0;

  return err;
}

static error_t
disk_pager_write_page (vm_offset_t page, void *buf)
{
  error_t err = 0;
  size_t length = vm_page_size, amount;
  vm_size_t dev_end = store->size;

  if (page + vm_page_size > dev_end)
    length = dev_end - page;

  ext2_debug ("writing disk page %ld[%zd]", page, length);

  STAT_INC (disk_pageouts);

  if (modified_global_blocks)
    /* Be picky about which blocks in a page that we write.  */
    {
      vm_offset_t offs = page;
      struct pending_blocks pb;

      pending_blocks_init (&pb, buf);

      while (length > 0 && !err)
	{
	  block_t block = boffs_block (offs);

	  /* We don't clear the block modified bit here because this paging
	     write request may not be the same one that actually set the bit,
	     and our copy of the page may be out of date; we have to leave
	     the bit on in case a paging write request corresponding to the
	     modification comes along later.  The bit is only actually ever
	     cleared if the block is allocated to a file, so this results in
	     excess writes of blocks from modified pages.  Unfortunately I
	     know of no way to get arount this given the current external
	     paging interface.  XXXX */
	  if (test_bit (block, modified_global_blocks))
	    /* This block may have been modified, so write it out.  */
	    err = pending_blocks_add (&pb, block);
	  else
	    /* Otherwise just skip it.  */
	    err = pending_blocks_skip (&pb);

	  offs += block_size;
	  length -= block_size;
	}

      if (!err)
	err = pending_blocks_write (&pb);
    }
  else
    {
      err = store_write (store, page >> store->log2_block_size,
			 buf, length, &amount);
      if (!err && length != amount)
	err = EIO;
    }

  return err;
}

/* Satisfy a pager read request for either the disk pager or file pager
   PAGER, to the page at offset PAGE into BUF.  WRITELOCK should be set if
   the pager should make the page writeable.  */
error_t
pager_read_page (struct user_pager_info *pager, vm_offset_t page,
		 vm_address_t *buf, int *writelock)
{
  if (pager->type == DISK)
    return disk_pager_read_page (page, (void **)buf, writelock);
  else
    return file_pager_read_page (pager->node, page, (void **)buf, writelock);
}

/* Satisfy a pager write request for either the disk pager or file pager
   PAGER, from the page at offset PAGE from BUF.  */
error_t
pager_write_page (struct user_pager_info *pager, vm_offset_t page,
		  vm_address_t buf)
{
  if (pager->type == DISK)
    return disk_pager_write_page (page, (void *)buf);
  else
    return file_pager_write_page (pager->node, page, (void *)buf);
}

/* Make page PAGE writable, at least up to ALLOCSIZE.  This function and
   diskfs_grow are the only places that blocks are actually added to the
   file.  */
error_t
pager_unlock_page (struct user_pager_info *pager, vm_offset_t page)
{
  if (pager->type == DISK)
    return 0;
  else
    {
      error_t err;
      volatile int partial_page;
      struct node *node = pager->node;
      struct disknode *dn = node->dn;

      rwlock_writer_lock (&dn->alloc_lock);

      partial_page = (page + vm_page_size > node->allocsize);

      err = diskfs_catch_exception ();
      if (!err)
	{
	  block_t block = page >> log2_block_size;
	  int left = (partial_page ? node->allocsize - page : vm_page_size);

	  while (left > 0)
	    {
	      block_t disk_block;
	      err = ext2_getblk (node, block++, 1, &disk_block);
	      if (err)
		break;
	      left -= block_size;
	    }
	}
      diskfs_end_catch_exception ();

      if (partial_page)
	/* If an error occurred, this page still isn't writable; otherwise,
	   since it's at the end of the file, it's now partially writable.  */
	dn->last_page_partially_writable = !err;
      else if (page + vm_page_size == node->allocsize)
	/* This makes the last page writable, which ends exactly at the end
	   of the file.  If any error occurred, the page still isn't
	   writable, and if not, then the whole thing is writable.  */
	dn->last_page_partially_writable = 0;

#ifdef EXT2FS_DEBUG
      if (dn->last_page_partially_writable)
	ext2_debug ("made page %u[%lu] in inode %d partially writable",
		    page, node->allocsize - page, node->cache_id);
      else
	ext2_debug ("made page %u[%u] in inode %d writable",
		    page, vm_page_size, node->cache_id);
#endif

      STAT_INC (file_page_unlocks);

      rwlock_writer_unlock (&dn->alloc_lock);

      if (err == ENOSPC)
	ext2_warning ("This filesystem is out of space, and will now crash.  Bye!");
      else if (err)
	ext2_warning ("inode=%Ld, page=0x%zx: %s",
		      node->cache_id, page, strerror (err));

      return err;
    }
}

/* Grow the disk allocated to locked node NODE to be at least SIZE bytes, and
   set NODE->allocsize to the actual allocated size.  (If the allocated size
   is already SIZE bytes, do nothing.)  CRED identifies the user responsible
   for the call.  */
error_t
diskfs_grow (struct node *node, off_t size, struct protid *cred)
{
  diskfs_check_readonly ();
  assert (!diskfs_readonly);

  if (size > node->allocsize)
    {
      error_t err = 0;
      off_t old_size;
      volatile off_t new_size;
      volatile block_t end_block;
      block_t new_end_block;
      struct disknode *dn = node->dn;

      rwlock_writer_lock (&dn->alloc_lock);

      old_size = node->allocsize;
      new_size = round_block (size);

      /* The first unallocated blocks after the old and new ends of the
	 file, respectively.  */
      end_block = old_size >> log2_block_size;
      new_end_block = new_size >> log2_block_size;

      if (new_end_block > end_block)
	{
	  /* The first block of the first unallocate page after the old end
	     of the file.  If LAST_PAGE_PARTIALLY_WRITABLE is true, any
	     blocks between this and END_BLOCK were unallocated, but are
	     considered `unlocked' -- that is pager_unlock_page has been
	     called on the page they're in.  Since after this grow the pager
	     will expect them to be writable, we'd better allocate them.  */
	  block_t old_page_end_block =
	    round_page (old_size) >> log2_block_size;

	  ext2_debug ("growing inode %Ld to %Lu bytes (from %Lu)",
		      node->cache_id, new_size, old_size);

	  if (dn->last_page_partially_writable
	      && old_page_end_block > end_block)
	    {
	      volatile block_t writable_end =
		(old_page_end_block > new_end_block
		 ? new_end_block
		 : old_page_end_block);

	      ext2_debug ("extending writable page %lu by %d blocks"
			  "; first new block = %u",
			  trunc_page (old_size),
			  writable_end - end_block,
			  end_block);

	      err = diskfs_catch_exception ();
	      while (!err && end_block < writable_end)
		{
		  block_t disk_block;
		  err = ext2_getblk (node, end_block++, 1, &disk_block);
		}
	      diskfs_end_catch_exception ();

	      if (! err)
		/* Reflect how much we allocated successfully.  */
		new_size = end_block << log2_block_size;
	      else
		/* See if it's still valid to say this.  */
		dn->last_page_partially_writable =
		  (old_page_end_block > end_block);
	    }
	}

      STAT_INC (file_grows);

      ext2_debug ("new size: %Lu%s.", new_size,
		  dn->last_page_partially_writable
		  ? " (last page writable)": "");
      if (err)
	ext2_warning ("inode=%Ld, target=%Lu: %s",
		      node->cache_id, new_size, strerror (err));

      node->allocsize = new_size;

      rwlock_writer_unlock (&dn->alloc_lock);

      return err;
    }
  else
    return 0;
}

/* This syncs a single file (NODE) to disk.  Wait for all I/O to complete
   if WAIT is set.  NODE->lock must be held.  */
void
diskfs_file_update (struct node *node, int wait)
{
  struct pager *pager;

  spin_lock (&node_to_page_lock);
  pager = node->dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (pager)
    {
      pager_sync (pager, wait);
      ports_port_deref (pager);
    }

  pokel_sync (&node->dn->indir_pokel, wait);

  diskfs_node_update (node, wait);
}

/* Invalidate any pager data associated with NODE.  */
void
flush_node_pager (struct node *node)
{
  struct pager *pager;
  struct disknode *dn = node->dn;

  spin_lock (&node_to_page_lock);
  pager = dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (pager)
    {
      pager_flush (pager, 1);
      ports_port_deref (pager);
    }
}


/* Return in *OFFSET and *SIZE the minimum valid address the pager will
   accept and the size of the object.  */
inline error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset, vm_size_t *size)
{
  assert (pager->type == DISK || pager->type == FILE_DATA);

  *offset = 0;

  if (pager->type == DISK)
    *size = store->size;
  else
    *size = pager->node->allocsize;

  return 0;
}

/* This is called when a pager is being deallocated after all extant send
   rights have been destroyed.  */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  if (upi->type == FILE_DATA)
    {
      struct pager *pager;

      spin_lock (&node_to_page_lock);
      pager = upi->node->dn->pager;
      if (pager && pager_get_upi (pager) == upi)
	upi->node->dn->pager = 0;
      spin_unlock (&node_to_page_lock);

      diskfs_nrele_light (upi->node);
    }

  free (upi);
}

/* This will be called when the ports library wants to drop weak references.
   The pager library creates no weak references itself.  If the user doesn't
   either, then it's OK for this function to do nothing.  */
void
pager_dropweak (struct user_pager_info *p __attribute__ ((unused)))
{
}

/* Create the DISK pager.  */
void
create_disk_pager (void)
{
  struct user_pager_info *upi = malloc (sizeof (struct user_pager_info));
  if (!upi)
    ext2_panic ("can't create disk pager: %s", strerror (errno));
  upi->type = DISK;
  pager_bucket = ports_create_bucket ();
  diskfs_start_disk_pager (upi, pager_bucket, MAY_CACHE, store->size,
			   &disk_image);
}

/* Call this to create a FILE_DATA pager and return a send right.
   NODE must be locked.  */
mach_port_t
diskfs_get_filemap (struct node *node, vm_prot_t prot)
{
  mach_port_t right;

  assert (S_ISDIR (node->dn_stat.st_mode)
	  || S_ISREG (node->dn_stat.st_mode)
	  || (S_ISLNK (node->dn_stat.st_mode)));

  spin_lock (&node_to_page_lock);
  do
    {
      struct pager *pager = node->dn->pager;
      if (pager)
	{
	  /* Because PAGER is not a real reference,
	     this might be nearly deallocated.  If that's so, then
	     the port right will be null.  In that case, clear here
	     and loop.  The deallocation will complete separately. */
	  right = pager_get_port (pager);
	  if (right == MACH_PORT_NULL)
	    node->dn->pager = 0;
	  else
	    pager_get_upi (pager)->max_prot |= prot;
	}
      else
	{
	  struct user_pager_info *upi =
	    malloc (sizeof (struct user_pager_info));
	  upi->type = FILE_DATA;
	  upi->node = node;
	  upi->max_prot = prot;
	  diskfs_nref_light (node);
	  node->dn->pager =
	    pager_create (upi, pager_bucket, MAY_CACHE,
			  MEMORY_OBJECT_COPY_DELAY);
	  if (node->dn->pager == 0)
	    {
	      diskfs_nrele_light (node);
	      free (upi);
	      spin_unlock (&node_to_page_lock);
	      return MACH_PORT_NULL;
	    }

	  right = pager_get_port (node->dn->pager);
	  ports_port_deref (node->dn->pager);
	}
    }
  while (right == MACH_PORT_NULL);
  spin_unlock (&node_to_page_lock);

  mach_port_insert_right (mach_task_self (), right, right,
			  MACH_MSG_TYPE_MAKE_SEND);

  return right;
}

/* Call this when we should turn off caching so that unused memory object
   ports get freed.  */
void
drop_pager_softrefs (struct node *node)
{
  struct pager *pager;

  spin_lock (&node_to_page_lock);
  pager = node->dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (MAY_CACHE && pager)
    {
      pager_sync (pager, 0);
      pager_change_attributes (pager, 0, MEMORY_OBJECT_COPY_DELAY, 0);
    }
  if (pager)
    ports_port_deref (pager);
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *node)
{
  struct pager *pager;

  spin_lock (&node_to_page_lock);
  pager = node->dn->pager;
  if (pager)
    ports_port_ref (pager);
  spin_unlock (&node_to_page_lock);

  if (MAY_CACHE && pager)
    pager_change_attributes (pager, 1, MEMORY_OBJECT_COPY_DELAY, 0);
  if (pager)
    ports_port_deref (pager);
}

/* Call this to find out the struct pager * corresponding to the
   FILE_DATA pager of inode IP.  This should be used *only* as a subsequent
   argument to register_memory_fault_area, and will be deleted when
   the kernel interface is fixed.  NODE must be locked.  */
struct pager *
diskfs_get_filemap_pager_struct (struct node *node)
{
  /* This is safe because pager can't be cleared; there must be
     an active mapping for this to be called. */
  return node->dn->pager;
}

/* Shutdown all the pagers (except the disk pager). */
void
diskfs_shutdown_pager ()
{
  error_t shutdown_one (void *v_p)
    {
      struct pager *p = v_p;
      if (p != diskfs_disk_pager)
	pager_shutdown (p);
      return 0;
    }

  write_all_disknodes ();

  ports_bucket_iterate (pager_bucket, shutdown_one);

  /* Sync everything on the the disk pager.  */
  sync_global (1);

  /* Despite the name of this function, we never actually shutdown the disk
     pager, just make sure it's synced. */
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  error_t sync_one (void *v_p)
    {
      struct pager *p = v_p;
      if (p != diskfs_disk_pager)
	pager_sync (p, wait);
      return 0;
    }

  write_all_disknodes ();
  ports_bucket_iterate (pager_bucket, sync_one);

  /* Do things on the the disk pager.  */
  sync_global (wait);
}

static void
disable_caching ()
{
  error_t block_cache (void *arg)
    {
      struct pager *p = arg;

      pager_change_attributes (p, 0, MEMORY_OBJECT_COPY_DELAY, 1);
      return 0;
    }

  /* Loop through the pagers and turn off caching one by one,
     synchronously.  That should cause termination of each pager. */
  ports_bucket_iterate (pager_bucket, block_cache);
}

static void
enable_caching ()
{
  error_t enable_cache (void *arg)
    {
      struct pager *p = arg;
      struct user_pager_info *upi = pager_get_upi (p);

      pager_change_attributes (p, 1, MEMORY_OBJECT_COPY_DELAY, 0);

      /* It's possible that we didn't have caching on before, because
	 the user here is the only reference to the underlying node
	 (actually, that's quite likely inside this particular
	 routine), and if that node has no links.  So dinkle the node
	 ref counting scheme here, which will cause caching to be
	 turned off, if that's really necessary.  */
      if (upi->type == FILE_DATA)
	{
	  diskfs_nref (upi->node);
	  diskfs_nrele (upi->node);
	}

      return 0;
    }

  ports_bucket_iterate (pager_bucket, enable_cache);
}

/* Tell diskfs if there are pagers exported, and if none, then
   prevent any new ones from showing up.  */
int
diskfs_pager_users ()
{
  int npagers = ports_count_bucket (pager_bucket);

  if (npagers <= 1)
    return 0;

  if (MAY_CACHE)
    {
      disable_caching ();

      /* Give it a second; the kernel doesn't actually shutdown
	 immediately.  XXX */
      sleep (1);

      npagers = ports_count_bucket (pager_bucket);
      if (npagers <= 1)
	return 0;

      /* Darn, there are actual honest users.  Turn caching back on,
	 and return failure. */
      enable_caching ();
    }

  ports_enable_bucket (pager_bucket);

  return 1;
}

/* Return the bitwise or of the maximum prot parameter (the second arg to
   diskfs_get_filemap) for all active user pagers. */
vm_prot_t
diskfs_max_user_pager_prot ()
{
  vm_prot_t max_prot = 0;
  int npagers = ports_count_bucket (pager_bucket);

  if (npagers > 1)
    /* More than just the disk pager.  */
    {
      error_t add_pager_max_prot (void *v_p)
	{
	  struct pager *p = v_p;
	  struct user_pager_info *upi = pager_get_upi (p);
	  if (upi->type == FILE_DATA)
	    max_prot |= upi->max_prot;
	  /* Stop iterating if MAX_PROT is as filled as it's going to get. */
	  return
	    (max_prot == (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)) ? 1 : 0;
	}

      disable_caching ();		/* Make any silly pagers go away. */

      /* Give it a second; the kernel doesn't actually shutdown
	 immediately.  XXX */
      sleep (1);

      ports_bucket_iterate (pager_bucket, add_pager_max_prot);

      enable_caching ();
    }

  ports_enable_bucket (pager_bucket);

  return max_prot;
}
