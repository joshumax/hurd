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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <hurd/store.h>
#include "ext2fs.h"

/* XXX */
#include "../libpager/priv.h"

/* A ports bucket to hold pager ports.  */
struct port_bucket *pager_bucket;

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

#define STAT_ADD(field, value)                                          \
  do { spin_lock (&ext2s_pager_stats.lock);                             \
    ext2s_pager_stats.field += value;                                   \
    spin_unlock (&ext2s_pager_stats.lock); } while (0)

#else /* !STATS */
#define STAT_INC(field) /* nop */0
#endif /* STATS */


/* Returns a page-aligned buffer.  */
static void * get_buf (size_t size)
{
  void *buf;
  posix_memalign (&buf, vm_page_size, round_page (size));
  return buf;
}

/* Frees a block returned by get_buf.  */
static inline void
free_buf (void *buf)
{
  free (buf);
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

/* Read LENGTH bytes for the pager backing NODE at offset PAGE, into BUF.
   This may need to read several filesystem blocks to satisfy range, and
   tries to consolidate the i/o if possible.  */
static void
file_pager_read (struct pager *pager, struct node *node,
		 off_t start, off_t npages)
{
  void *buf;
  error_t err;
  int offs = 0;
  int partial = 0;		/* A page truncated by the EOF.  */
  size_t length = npages * vm_page_size;
  vm_offset_t page;
  int left = length;
  struct rwlock *lock = NULL;
  block_t pending_blocks = 0;
  int num_pending_blocks = 0;
  int writelock = 0, precious = 0, deallocate = 1;

  assert (left > 0);

  page = start * vm_page_size;

  ext2_debug ("reading inode %Ld page %jd[%jd]",
	      node->cache_id, start, npages);

  /* Read the NUM_PENDING_BLOCKS blocks in PENDING_BLOCKS, into the buffer
     pointed to by BUF (allocating it if necessary) at offset OFFS.  OFFS in
     adjusted by the amount read, and NUM_PENDING_BLOCKS is zeroed.  Any read
     error is returned.  */
  error_t do_pending_reads ()
    {
      if (num_pending_blocks > 0)
	{
	  store_offset_t dev_block = (store_offset_t) pending_blocks
	    << log2_dev_blocks_per_fs_block;
	  size_t amount = num_pending_blocks << log2_block_size;
	  /* The buffer we try to read into; on the first read, we pass in a
	     size of zero, so that the read is guaranteed to allocate a new
	     buffer, otherwise, we try to read directly into the tail of the
	     buffer we've already got.  */
	  void *new_buf = buf + offs;
	  size_t new_len = length - offs;

	  STAT_INC (file_pagein_reads);

	  err = store_read (store, dev_block, amount, &new_buf, &new_len);
	  if (err)
	    return err;
	  else if (amount != new_len)
	    return EIO;

	  if (new_buf != buf + offs)
	    {
	      /* The read went into a different buffer than the one we
		 passed. */
	      memcpy (buf + offs, new_buf, new_len);
	      munmap (new_buf, new_len);
	      STAT_ADD (file_pagein_freed_bufs, npages);
	    }

	  offs += new_len;
	  num_pending_blocks = 0;
	}

      return 0;
    }

  STAT_INC (file_pageins);

#define page_aligned(addr) (((size_t) addr & (vm_page_size - 1)) == 0)
  assert (page_aligned (page) && page_aligned (left) &&
	  page_aligned (node->allocsize));
#undef page_aligned

  if (page >= node->allocsize)
    {
      err = EIO;
      left = 0;
    }
  else if (page + left > node->allocsize)
    {
      size_t last_page = node->allocsize >> log2_block_size;
      size_t tail = start + npages - last_page;

      pager_data_read_error (pager, last_page, tail, KERN_NO_DATA);

      left = node->allocsize - page;
      length = left;
      partial = 1;
    }

  buf = mmap (0, length, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
  if (!buf)
    {
      err = ENOMEM;
      goto end;
    }
  STAT_ADD (file_pagein_alloced_bufs, npages);

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
	  writelock = 1;
	  memset (buf + offs, 0, block_size);
	  offs += block_size;
	}
      else
	num_pending_blocks++;

      page += block_size;
      left -= block_size;
    }

 end:
  if (!err && num_pending_blocks > 0)
    err = do_pending_reads();

  if (!err && partial && !writelock)
    node->dn->last_page_partially_writable = 1;

  if (lock)
    rwlock_reader_unlock (lock);

  /* Note that amount of returned data could change, so update NPAGES */
  npages = length >> log2_block_size;
  if (err)
    pager_data_read_error (pager, start, npages, err);
  else
    pager_data_supply (pager, precious, writelock, start, npages,
		       buf, deallocate);
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
      store_offset_t dev_block = (store_offset_t) pb->block
	<< log2_dev_blocks_per_fs_block;
      size_t length = pb->num << log2_block_size, amount;

      ext2_debug ("writing block %u[%Ld]", pb->block, pb->num);

      if (pb->offs > 0)
	/* Put what we're going to write into a page-aligned buffer.  */
	{
	  void *page_buf = get_buf (length);
	  memcpy ((void *)page_buf, pb->buf + pb->offs, length);
	  err = store_write (store, dev_block, page_buf, length, &amount);
	  free_buf (page_buf);
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

/* Write LENGTH bytes for the pager backing NODE, at offset PAGE, into BUF.
   This may need to write several filesystem blocks to satisfy range, and
   tries to consolidate the i/o if possible.  */
static void
file_pager_write (struct pager * pager, struct node *node,
		  vm_offset_t start, size_t npages, void *buf)
{
  error_t err = 0;
  struct pending_blocks pb;
  struct rwlock *lock = &node->dn->alloc_lock;
  block_t block;
  int left = npages * vm_page_size;
  vm_offset_t offset = start * vm_page_size;

  assert (left > 0);

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

  if (err)
    pager_data_write_error (pager, start, npages, err);
}

static void
disk_pager_read (struct pager *pager, off_t start_page, size_t npages)
{
  size_t left = npages;
  store_offset_t offset = start_page, dev_end = store->size;

  assert (block_size == vm_page_size);

  void supply_data (off_t start, size_t nblocks)
  {
    void *buf;
    error_t err;
    size_t read = 0, length = nblocks << log2_block_size;
    const int writelock = 0, precious = 0, deallocate = 1;

    off_t get_addr = (((store_offset_t) disk_cache_info[start].block << log2_block_size)
		      >> store->log2_block_size);
    err = store_read (store, get_addr, length, &buf, &read);

    if (!err && (read == length))
      pager_data_supply (pager, precious, writelock, start, nblocks,
			 buf, deallocate);
    else
      pager_data_read_error (pager, start, nblocks, EIO);
  }

  if (offset + left > dev_end >> log2_block_size)
    left = (dev_end >> log2_block_size) - offset;
  if (!left)
    return;

  int i;
  store_offset_t range = 0;
  off_t base = start_page;
  for (i = start_page; i < start_page + left; i++)
    {
      mutex_lock (&disk_cache_lock);
      if (disk_cache_info[i].block == DC_NO_BLOCK)
	{
	  block_t block = disk_cache_info[i - 1].block + 1;
	  if (disk_cache_block_is_cached (block))
	    {
	      mutex_unlock (&disk_cache_lock);
	      supply_data (base, range);

	      /* Block already in cache. Do not return it again.*/
	      pager_data_read_error (pager, i, 1, KERN_NO_DATA);

	      base += range;
	      range = 0;
	      continue;
	    }
	  else
	    {
	      void *addr;
	      addr = disk_cache_block_ref_hint_no_block (block, i);
	      assert (bptr_index (addr) == i);
	    }
	}

      assert (disk_cache_info[i].block != DC_NO_BLOCK);

      ext2_debug ("block %x log2 %x", disk_cache_info[i].block, log2_block_size);
      disk_cache_info[i].flags |= DC_INCORE;
      disk_cache_info[i].flags &=~ DC_UNTOUCHED;
#ifndef NDEBUG
      disk_cache_info[i].last_read = disk_cache_info[i].block;
      disk_cache_info[i].last_read_xor
	= disk_cache_info[i].block ^ DISK_CACHE_LAST_READ_XOR;
#endif
      ext2_debug ("(%Ld)[%Ld]", base, range);
      mutex_unlock (&disk_cache_lock);

      if (disk_cache_info[base].block + range !=
	  disk_cache_info[base + range].block)
	{
	  supply_data (base, range);
	  base += range;
	  range = 0;
	}

      range++;
    }

  if (range)
    supply_data (base, range);
}

static void
disk_pager_write (struct pager * pager, vm_offset_t start,
		  size_t npages, void *buf)
{
  size_t dev_end = store->size;
  size_t left = npages;

  assert (block_size == vm_page_size);

  void write_data (off_t write, off_t notice, size_t nblocks, void *buf)
  {
    error_t err;
    size_t amount, length = nblocks << log2_block_size;

    err = store_write (store, write, buf, length, &amount);

    if (err)
      pager_data_write_error (pager, notice, npages, err);
    if (length != amount)
      pager_data_write_error (pager, notice, npages, EIO);
  }

  if (start + left > dev_end >> log2_block_size)
    left = (dev_end >> log2_block_size) - start;

  ext2_debug ("writing disk page %ld[%zd]", start, npages);

  STAT_INC (disk_pageouts);

  if (modified_global_blocks)
    /* Be picky about which blocks in a page that we write.  */
    {
      assert ("This code should be dead since only page sized blocks"
	      "are supported." && 0);
#if 0
      struct pending_blocks pb;

      pending_blocks_init (&pb, buf);

      while (length > 0 && !err)
	{
	  block_t block = boffs_block (offset);

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

	  offset += block_size;
	  length -= block_size;
	}

      if (!err)
	err = pending_blocks_write (&pb);
#endif
    }
  else
    {
      int i;
      size_t range = 0;
      store_offset_t offset = start * block_size;
      store_offset_t start_send = start, start_info = start;

      for (i = start; i < start + left; i++)
	{
	  store_offset_t new_offset;

	  mutex_lock (&disk_cache_lock);
	  assert (disk_cache_info[i].block != DC_NO_BLOCK);
	  new_offset = ((store_offset_t) disk_cache_info[i].block
			<< log2_block_size);
#ifndef NDEBUG			/* Not strictly needed.  */
	  assert ((disk_cache_info[i].last_read ^ DISK_CACHE_LAST_READ_XOR)
		  == disk_cache_info[i].last_read_xor);
	  assert (disk_cache_info[i].last_read
		  == disk_cache_info[i].block);
#endif
	  mutex_unlock (&disk_cache_lock);

	  if (range == 0)
	      start_send = new_offset >> store->log2_block_size;
	  else if (new_offset != offset + block_size)
	    {
	      write_data (start_send, start_info, range, buf);
	      buf += range * block_size;
	      start_send = new_offset >> store->log2_block_size;
	      start_info += range;
	      range = 0;
	    }

	  offset = new_offset;
	  range++;
	}
      write_data (start_send, start_info, range, buf);
    }
}


/* Satisfy a pager read request for either the disk pager or file pager
   PAGER, from the NPAGES pages pointed to be BUF starting at page START.
   WRITELOCK should be set if the pager should make the page writeable.  */
void
ext2_read_pages (struct pager *pager, struct user_pager_info *upi,
		 off_t start, off_t npages)
{
  if (upi->type == DISK)
    disk_pager_read (pager, start, npages);
  else
    file_pager_read (pager, upi->node, start, npages);
}

/* Satisfy a pager write request for either the disk pager or file pager
   PAGER, from the NPAGES pages pointed to be BUF starting at page START.*/
void
ext2_write_pages (struct pager *pager, struct user_pager_info *upi,
		  off_t start, off_t npages, void *buf, int dealloc)
{
  if (upi->type == DISK)
    disk_pager_write (pager, start, npages, buf);
  else
    file_pager_write (pager, upi->node, start, npages, buf);

  if (dealloc)
    vm_deallocate (mach_task_self (), (vm_address_t) buf,
		   npages * vm_page_size);
}

void
ext2_notify_evict (struct user_pager_info *pager, off_t page,
		   off_t npages)
{
  unsigned index = page >> log2_block_size;
  unsigned last_page = index + npages;

  if (pager->type != DISK)
    return;

  ext2_debug ("(block %u %u)", index, last_page);

  mutex_lock (&disk_cache_lock);
  for (; index < last_page; index ++)
    disk_cache_info[index].flags &= ~DC_INCORE;
  mutex_unlock (&disk_cache_lock);
}


  /* Make the NPAGES pages, starting at page START writable, at least
     up to ALLOCSIZE.  This function and diskfs_grow are the only places
     that blocks are actually added to the file.  */
void
ext2_unlock_pages (struct pager *pager,
		   struct user_pager_info *upi,
		   off_t start, off_t npages)
{
  if (upi->type == DISK)
    return;

  error_t err;
  volatile int partial_page;
  struct node *node = upi->node;
  struct disknode *dn = node->dn;
  block_t block = (start * vm_page_size) >> log2_block_size;
  off_t end = (start + npages) * vm_page_size;

  inline error_t process_code (off_t block_offset)
  {
    block_t disk_block;
    return ext2_getblk (node, block++, 1, &disk_block);
  }

  inline void no_error_code (off_t range_start, off_t range_len)
  {
    pager_data_unlock (pager, range_start, range_len / vm_page_size);
#ifdef EXT2FS_DEBUG
    ext2_debug ("made pages %jd[%jd] in inode %llu writable",
		range_start / vm_page_size, range_len / vm_page_size,
		node->cache_id);
#endif
  }

  inline void error_code (error_t err, off_t range_start, off_t range_len)
  {
    off_t err_pages = round_page (range_len) / vm_page_size;
    pager_data_unlock_error (pager, range_start, err_pages, err);

    if (err == ENOSPC)
      ext2_warning ("This filesystem is out of space, and will now"
		    " crash.  Bye!");
    else if (err)
      ext2_warning ("inode=%Ld, pages=0x%llx[%lld]: %s",
		    node->cache_id, range_start, range_len, strerror (err));

    block += (round_page (range_len) - range_len) / block_size;
  }

  rwlock_writer_lock (&dn->alloc_lock);

  partial_page = (end > node->allocsize);

  err = diskfs_catch_exception ();
  if (!err)
    pager_process_pages (start, npages, block_size,
			 process_code, no_error_code, error_code);
  diskfs_end_catch_exception ();

  if (partial_page)
    /* If an error occurred, this page still isn't writable; otherwise,
       since it's at the end of the file, it's now partially writable.  */
    dn->last_page_partially_writable = !err;
  else if (end == node->allocsize)
    /* This makes the last page writable, which ends exactly at the end
       of the file.  If any error occurred, the page still isn't
       writable, and if not, then the whole thing is writable.  */
    dn->last_page_partially_writable = 0;

#ifdef EXT2FS_DEBUG
  if (dn->last_page_partially_writable)
    ext2_debug ("made page %jd[%jd] in inode %llu partially writable",
		end / vm_page_size, end % vm_page_size, node->cache_id);
#endif

  STAT_INC (file_page_unlocks);

  rwlock_writer_unlock (&dn->alloc_lock);
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
void
ext2_report_extent (struct user_pager_info *upi,
		    off_t *start, off_t *end)
{
  assert (upi->type == DISK || upi->type == FILE_DATA);

  *start = 0;

  if (upi->type == DISK)
    *end = round_page (store->size) / vm_page_size;
  else
    *end = round_page (upi->node->allocsize) / vm_page_size;
}

/* This is called when a pager is being deallocated after all extant send
   rights have been destroyed.  */
void
ext2_clear_user_data (struct user_pager_info *upi)
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
}

struct pager_ops ext2_ops =
  {
    .read = &ext2_read_pages,
    .write = &ext2_write_pages,
    .unlock = &ext2_unlock_pages,
    .report_extent = &ext2_report_extent,
    .clear_user_data = &ext2_clear_user_data,
    .notify_evict = &ext2_notify_evict,
    .dropweak = NULL
  };


/* Cached blocks from disk.  */
void *disk_cache;

/* DISK_CACHE size in bytes and blocks.  */
store_offset_t disk_cache_size;
int disk_cache_blocks;

/* block num --> pointer to in-memory block */
hurd_ihash_t disk_cache_bptr;
/* Cached blocks' info.  */
struct disk_cache_info *disk_cache_info;
/* Hint index for which cache block to reuse next.  */
int disk_cache_hint;
/* Lock for these structures.  */
struct mutex disk_cache_lock;
/* Fired when a re-association is done.  */
struct condition disk_cache_reassociation;

/* Finish mapping initialization. */
static void
disk_cache_init (void)
{
  if (block_size != vm_page_size)
    ext2_panic ("Block size %d != vm_page_size %d",
		block_size, vm_page_size);

  mutex_init (&disk_cache_lock);
  condition_init (&disk_cache_reassociation);

  /* Allocate space for block num -> in-memory pointer mapping.  */
  if (hurd_ihash_create (&disk_cache_bptr, HURD_IHASH_NO_LOCP))
    ext2_panic ("Can't allocate memory for disk_pager_bptr");

  /* Allocate space for disk cache blocks' info.  */
  disk_cache_info = malloc ((sizeof *disk_cache_info) * disk_cache_blocks);
  if (!disk_cache_info)
    ext2_panic ("Cannot allocate space for disk cache info");

  /* Initialize disk_cache_info.  */
  for (int i = 0; i < disk_cache_blocks; i++)
    {
      disk_cache_info[i].block = DC_NO_BLOCK;
      disk_cache_info[i].flags = 0;
      disk_cache_info[i].ref_count = 0;
#ifndef NDEBUG
      disk_cache_info[i].last_read = DC_NO_BLOCK;
      disk_cache_info[i].last_read_xor
	= DC_NO_BLOCK ^ DISK_CACHE_LAST_READ_XOR;
#endif
    }
  disk_cache_hint = 0;

  /* Map the superblock and the block group descriptors.  */
  block_t fixed_first = boffs_block (SBLOCK_OFFS);
  block_t fixed_last = fixed_first
    + (round_block ((sizeof *group_desc_image) * groups_count)
       >> log2_block_size);
  ext2_debug ("%d-%d\n", fixed_first, fixed_last);
  assert (fixed_last - fixed_first + 1 <= (block_t)disk_cache_blocks + 3);
  for (block_t i = fixed_first; i <= fixed_last; i++)
    {
      disk_cache_block_ref (i);
      assert (disk_cache_info[i-fixed_first].block == i);
      disk_cache_info[i-fixed_first].flags |= DC_FIXED;
    }
}

static void
disk_cache_return_unused (void)
{
  int index;

  /* XXX: Touch all pages.  It seems that sometimes GNU Mach "forgets"
     to notify us about evicted pages.  Disk cache must be
     unlocked.  */
  for (vm_offset_t i = 0; i < disk_cache_size; i += vm_page_size)
    *(volatile char *)(disk_cache + i);

  /* Release some references to cached blocks.  */
  pokel_sync (&global_pokel, 1);

  /* Return unused pages that are in core.  */
  int pending_begin = -1, pending_end = -1;
  mutex_lock (&disk_cache_lock);
  for (index = 0; index < disk_cache_blocks; index++)
    if (! (disk_cache_info[index].flags & (DC_DONT_REUSE & ~DC_INCORE))
	&& ! disk_cache_info[index].ref_count)
      {
	ext2_debug ("return %u -> %d",
		    disk_cache_info[index].block, index);
	if (index != pending_end)
	  {
	    /* Return previous region, if there is such, ... */
	    if (pending_end >= 0)
	      {
		mutex_unlock (&disk_cache_lock);
		pager_return_some (diskfs_disk_pager,
				   pending_begin * vm_page_size,
				   (pending_end - pending_begin)
				   * vm_page_size,
				   1);
		mutex_lock (&disk_cache_lock);
	      }
	    /* ... and start new region.  */
	    pending_begin = index;
	  }
	pending_end = index + 1;
      }

  mutex_unlock (&disk_cache_lock);

  /* Return last region, if there is such.   */
  if (pending_end >= 0)
    pager_return_some (diskfs_disk_pager,
		       pending_begin * vm_page_size,
		       (pending_end - pending_begin) * vm_page_size,
		       1);
  else
    {
      printf ("ext2fs: disk cache is starving\n");

      /* Give it some time.  This should happen rarely.  */
      sleep (1);
    }
}

/* Map block and return pointer to it.  */
void *
disk_cache_block_ref_no_block (block_t block)
{
  int index;
  void *bptr;

  assert (0 <= block && block < store->size >> log2_block_size);

  ext2_debug ("(%u)", block);

  bptr = hurd_ihash_find (disk_cache_bptr, block);
  ext2_debug ("yaya (%u) %p", block, bptr);
  if (bptr)
    /* Already mapped.  */
    {
      index = bptr_index (bptr);

      /* In process of re-associating?  */
      if (disk_cache_info[index].flags & DC_UNTOUCHED)
	{
	  /* Wait re-association to finish.  */
	  condition_wait (&disk_cache_reassociation, &disk_cache_lock);

#if 0
	  printf ("Re-association -- wait finished.\n");
#endif

	  /* Try again.  */
	  return disk_cache_block_ref_no_block (block); /* tail recursion */
	}

      /* Just increment reference and return.  */
      assert (disk_cache_info[index].ref_count + 1
	      > disk_cache_info[index].ref_count);
      disk_cache_info[index].ref_count++;

      ext2_debug ("cached %u -> %d (ref_count = %d, flags = 0x%x, ptr = %p)",
		  disk_cache_info[index].block, index,
		  disk_cache_info[index].ref_count,
		  disk_cache_info[index].flags, bptr);

      return bptr;
    }

  /* Search for a block that is not in core and is not referenced.  */
  index = disk_cache_hint;
  while ((disk_cache_info[index].flags & DC_DONT_REUSE)
	 || (disk_cache_info[index].ref_count))
    {
      ext2_debug ("reject %u -> %d (ref_count = %d, flags = 0x%x)",
		  disk_cache_info[index].block, index,
		  disk_cache_info[index].ref_count,
		  disk_cache_info[index].flags);

      /* Just move to next block.  */
      index++;
      if (index >= disk_cache_blocks)
	index -= disk_cache_blocks;

      /* If we return to where we started, than there is no suitable
	 block. */
      if (index == disk_cache_hint)
	break;
    }

  /* The next place in the disk cache becomes the current hint.  */
  disk_cache_hint = index + 1;
  if (disk_cache_hint >= disk_cache_blocks)
    disk_cache_hint -= disk_cache_blocks;

  /* Is suitable place found?  */
  if ((disk_cache_info[index].flags & DC_DONT_REUSE)
      || disk_cache_info[index].ref_count)
    /* No place is found.  Try to release some blocks and try
       again.  */
    {
      ext2_debug ("flush %u -> %d", disk_cache_info[index].block, index);

      disk_cache_return_unused ();

      return disk_cache_block_ref_no_block (block); /* tail recursion */
    }

  /* Suitable place is found.  */

  /* Calculate pointer to data.  */
  bptr = (char *)disk_cache + (index << log2_block_size);
  ext2_debug ("map %u -> %d (%p)", block, index, bptr);

  /* This pager_return_some is used only to set PM_FORCEREAD for the
     page.  DC_UNTOUCHED is set so that we catch if someone has
     referenced the block while we didn't hold disk_cache_lock.  */
  disk_cache_info[index].flags |= DC_UNTOUCHED;

  /* Re-associate.  */
  if (disk_cache_info[index].block != DC_NO_BLOCK)
    /* Remove old association.  */
    hurd_ihash_remove (disk_cache_bptr, disk_cache_info[index].block);
  /* New association.  */
  if (hurd_ihash_add (disk_cache_bptr, block, bptr))
    ext2_panic ("Couldn't hurd_ihash_add new disk block");
  assert (! (disk_cache_info[index].flags & DC_DONT_REUSE & ~DC_UNTOUCHED));
  disk_cache_info[index].block = block;
  assert (! disk_cache_info[index].ref_count);
  disk_cache_info[index].ref_count = 1;

  condition_broadcast (&disk_cache_reassociation);

  /* Note that in contrast to blocking version of this function, this
     function should *NEVER* try to read page, as this function is called
     when previous page in cache is asked. Thus reading of this page could
     lead to situation, when kernel will ask next page and ultimately to
     recursion. */

  ext2_debug ("(%u) = %p", block, bptr);
  return bptr;
}

int disk_cache_block_is_cached (block_t block)
{
  void *a = hurd_ihash_find (disk_cache_bptr, block);
  ext2_debug ("%p", a);
  return (int)a;
}

/* Map block and return pointer to it.  */
void *
disk_cache_block_ref (block_t block)
{
  int index;
  void *bptr;

  assert (0 <= block && block < store->size >> log2_block_size);

  ext2_debug ("(%u)", block);

  mutex_lock (&disk_cache_lock);

  bptr = hurd_ihash_find (disk_cache_bptr, block);
  if (bptr)
    /* Already mapped.  */
    {
      index = bptr_index (bptr);

      /* In process of re-associating?  */
      if (disk_cache_info[index].flags & DC_UNTOUCHED)
	{
	  /* Wait re-association to finish.  */
	  condition_wait (&disk_cache_reassociation, &disk_cache_lock);
	  mutex_unlock (&disk_cache_lock);

#if 0
	  printf ("Re-association -- wait finished.\n");
#endif

	  /* Try again.  */
	  return disk_cache_block_ref (block); /* tail recursion */
	}

      /* Just increment reference and return.  */
      assert (disk_cache_info[index].ref_count + 1
	      > disk_cache_info[index].ref_count);
      disk_cache_info[index].ref_count++;

      ext2_debug ("cached %u -> %d (ref_count = %d, flags = 0x%x, ptr = %p)",
		  disk_cache_info[index].block, index,
		  disk_cache_info[index].ref_count,
		  disk_cache_info[index].flags, bptr);

      mutex_unlock (&disk_cache_lock);

      return bptr;
    }

  /* Search for a block that is not in core and is not referenced.  */
  index = disk_cache_hint;
  while ((disk_cache_info[index].flags & DC_DONT_REUSE)
	 || (disk_cache_info[index].ref_count))
    {
      ext2_debug ("reject %u -> %d (ref_count = %d, flags = 0x%x)",
		  disk_cache_info[index].block, index,
		  disk_cache_info[index].ref_count,
		  disk_cache_info[index].flags);

      /* Just move to next block.  */
      index++;
      if (index >= disk_cache_blocks)
	index -= disk_cache_blocks;

      /* If we return to where we started, than there is no suitable
	 block. */
      if (index == disk_cache_hint)
	break;
    }

  /* The next place in the disk cache becomes the current hint.  */
  disk_cache_hint = index + 1;
  if (disk_cache_hint >= disk_cache_blocks)
    disk_cache_hint -= disk_cache_blocks;

  /* Is suitable place found?  */
  if ((disk_cache_info[index].flags & DC_DONT_REUSE)
      || disk_cache_info[index].ref_count)
    /* No place is found.  Try to release some blocks and try
       again.  */
    {
      ext2_debug ("flush %u -> %d", disk_cache_info[index].block, index);

      mutex_unlock (&disk_cache_lock);

      disk_cache_return_unused ();

      return disk_cache_block_ref (block); /* tail recursion */
    }

  /* Suitable place is found.  */

  /* Calculate pointer to data.  */
  bptr = (char *)disk_cache + (index << log2_block_size);
  ext2_debug ("map %u -> %d (%p)", block, index, bptr);

  /* This pager_return_some is used only to set PM_FORCEREAD for the
     page.  DC_UNTOUCHED is set so that we catch if someone has
     referenced the block while we didn't hold disk_cache_lock.  */
  disk_cache_info[index].flags |= DC_UNTOUCHED;

#if 0 /* XXX: Let's see if this is needed at all.  */

  mutex_unlock (&disk_cache_lock);
  pager_return_some (diskfs_disk_pager, bptr - disk_cache, vm_page_size, 1);
  mutex_lock (&disk_cache_lock);

  /* Has someone used our bptr?  Has someone mapped requested block
     while we have unlocked disk_cache_lock?  If so, environment has
     changed and we have to restart operation.  */
  if ((! (disk_cache_info[index].flags & DC_UNTOUCHED))
      || hurd_ihash_find (disk_cache_bptr, block))
    {
      mutex_unlock (&disk_cache_lock);
      return disk_cache_block_ref (block); /* tail recursion */
    }

#elif 0

  /* XXX: Use libpager internals.  */

  mutex_lock (&diskfs_disk_pager->interlock);
  int page = (bptr - disk_cache) / vm_page_size;
  assert (page >= 0);
  int is_incore = (page < diskfs_disk_pager->pagemapsize
		   && (diskfs_disk_pager->pagemap[page] & PM_INCORE));
  mutex_unlock (&diskfs_disk_pager->interlock);
  if (is_incore)
    {
      mutex_unlock (&disk_cache_lock);
      printf ("INCORE\n");
      return disk_cache_block_ref (block); /* tail recursion */
    }

#endif

  /* Re-associate.  */
  if (disk_cache_info[index].block != DC_NO_BLOCK)
    /* Remove old association.  */
    hurd_ihash_remove (disk_cache_bptr, disk_cache_info[index].block);
  /* New association.  */
  if (hurd_ihash_add (disk_cache_bptr, block, bptr))
    ext2_panic ("Couldn't hurd_ihash_add new disk block");
  assert (! (disk_cache_info[index].flags & DC_DONT_REUSE & ~DC_UNTOUCHED));
  disk_cache_info[index].block = block;
  assert (! disk_cache_info[index].ref_count);
  disk_cache_info[index].ref_count = 1;

  /* All data structures are set up.  */
  mutex_unlock (&disk_cache_lock);

#if 0
  /* Try to read page.  */
  *(volatile char *) bptr;

  /* Check if it's actually read.  */
  mutex_lock (&disk_cache_lock);
  if (disk_cache_info[index].flags & DC_UNTOUCHED)
    /* It's not read.  */
    {
      /* Remove newly created association.  */
      hurd_ihash_remove (disk_cache_bptr, block);
      disk_cache_info[index].block = DC_NO_BLOCK;
      disk_cache_info[index].flags &=~ DC_UNTOUCHED;
      disk_cache_info[index].ref_count = 0;
      mutex_unlock (&disk_cache_lock);

      /* Prepare next time association of this page to succeed.  */
      pager_flush_some (diskfs_disk_pager, bptr - disk_cache,
			vm_page_size, 0);

#if 0
      printf ("Re-association failed.\n");
#endif

      /* Try again.  */
      return disk_cache_block_ref (block); /* tail recursion */
    }
  mutex_unlock (&disk_cache_lock);
#endif

  /* Re-association was successful.  */
  condition_broadcast (&disk_cache_reassociation);

  ext2_debug ("(%u) = %p", block, bptr);
  return bptr;
}

void *
disk_cache_block_ref_hint_no_block (block_t block, int hint)
{
  void * result;
  int old_hint = disk_cache_hint;
  disk_cache_hint = hint;
  result = disk_cache_block_ref_no_block (block);
  disk_cache_hint = hint == old_hint ? old_hint + 1 : old_hint;
  return result;
}

void
disk_cache_block_ref_ptr (void *ptr)
{
  int index;

  mutex_lock (&disk_cache_lock);
  index = bptr_index (ptr);
  assert (disk_cache_info[index].ref_count >= 1);
  assert (disk_cache_info[index].ref_count + 1
	  > disk_cache_info[index].ref_count);
  disk_cache_info[index].ref_count++;
  assert (! (disk_cache_info[index].flags & DC_UNTOUCHED));
  ext2_debug ("(%p) (ref_count = %d, flags = 0x%x)",
	      ptr,
	      disk_cache_info[index].ref_count,
	      disk_cache_info[index].flags);
  mutex_unlock (&disk_cache_lock);
}

void
disk_cache_block_deref (void *ptr)
{
  int index;

  assert (disk_cache <= ptr && ptr <= disk_cache + disk_cache_size);

  mutex_lock (&disk_cache_lock);
  index = bptr_index (ptr);
  ext2_debug ("(%p) (ref_count = %d, flags = 0x%x)",
	      ptr,
	      disk_cache_info[index].ref_count - 1,
	      disk_cache_info[index].flags);
  assert (! (disk_cache_info[index].flags & DC_UNTOUCHED));
  assert (disk_cache_info[index].ref_count >= 1);
  disk_cache_info[index].ref_count--;
  mutex_unlock (&disk_cache_lock);
}

/* Not used.  */
int
disk_cache_block_is_ref (block_t block)
{
  int ref;
  void *ptr;

  mutex_lock (&disk_cache_lock);
  ptr = hurd_ihash_find (disk_cache_bptr, block);
  if (! ptr)
    ref = 0;
  else				/* XXX: Should check for DC_UNTOUCHED too.  */
    ref = disk_cache_info[bptr_index (ptr)].ref_count;
  mutex_unlock (&disk_cache_lock);

  return ref;
}

/* Create the DISK pager.  */
void
create_disk_pager (void)
{
  pager_bucket = ports_create_bucket ();
  get_hypermetadata ();
  disk_cache_blocks = DISK_CACHE_BLOCKS;
  disk_cache_size = disk_cache_blocks << log2_block_size;
  diskfs_start_disk_pager (&ext2_ops, sizeof (struct user_pager_info),
			   pager_bucket, MAY_CACHE, disk_cache_size,
			   &disk_cache);
  pager_get_upi (diskfs_disk_pager)->type = DISK;
  disk_cache_init ();
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
	  struct user_pager_info *upi;
	  diskfs_nref_light (node);
	  node->dn->pager = pager_create (&ext2_ops, sizeof (*upi),
					  pager_bucket, MAY_CACHE,
					  MEMORY_OBJECT_COPY_DELAY);
	  if (node->dn->pager == 0)
	    {
	      diskfs_nrele_light (node);
	      spin_unlock (&node_to_page_lock);
	      return MACH_PORT_NULL;
	    }

	  upi = pager_get_upi (node->dn->pager);
	  upi->type = FILE_DATA;
	  upi->node = node;
	  upi->max_prot = prot;

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
