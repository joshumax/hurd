/* Pager for ext2fs

   Copyright (C) 1994, 1995 Free Software Foundation, Inc.

   Converted for ext2fs by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <strings.h>
#include "ext2fs.h"

/* A ports bucket to hold pager ports.  */
static struct port_bucket *pager_bucket;

spin_lock_t node_to_page_lock = SPIN_LOCK_INITIALIZER;

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

/* ---------------------------------------------------------------- */

/* Find the location on disk of page OFFSET in NODE.  Return the disk block
   in BLOCK (if unallocated, then return 0).  If *LOCK is 0, then it a reader
   lock is aquired on NODE's ALLOC_LOCK before doing anything, and left
   locked after return -- even if an error is returned.  0 on success or an
   error code otherwise is returned.  */
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

/* ---------------------------------------------------------------- */

/* Read one page for the pager backing NODE at offset PAGE, into BUF.  This
   may need to read several filesystem blocks to satisfy one page, and tries
   to consolidate the i/o if possible.  */
static error_t
file_pager_read_page (struct node *node, vm_offset_t page,
		      vm_address_t *buf, int *writelock)
{
  error_t err;
  int offs = 0;
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
	  int length = num_pending_blocks << log2_block_size;
	  vm_address_t new_buf;

	  err = dev_read_sync (dev_block, &new_buf, length);
	  if (err)
	    return err;

	  if (offs == 0)
	    /* First read, make the returned page be our buffer.  */
	    *buf = new_buf;
	  else
	    {
	      /* We've already got some buffer, so copy into it.  */
	      bcopy ((char *)new_buf, (char *)*buf + offs, length);
	      vm_deallocate (mach_task_self (), new_buf, length);
	    }

	  offs += length;
	  num_pending_blocks = 0;
	}

      return 0;
    }

  if (page + left > node->allocsize)
    left = node->allocsize - page;

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
	/* Reading unallocate block, just make a zero-filled one.  */
	{
	  *writelock = 1;
	  if (offs == 0)
	    /* No page allocated to read into yet.  */
	    {
	      err = vm_allocate (mach_task_self (), buf, vm_page_size, 1);
	      if (err)
		break;
	    }
	  bzero ((char *)*buf + offs, block_size);
	  offs += block_size;
	}
      else
	num_pending_blocks++;

      page += block_size;
      left -= block_size;
    }

  if (!err && num_pending_blocks > 0)
    do_pending_reads();
      
  if (lock)
    rwlock_reader_unlock (lock);

  return err;
}

/* ---------------------------------------------------------------- */

struct pending_blocks
{
  /* The block number of the first of the blocks.  */
  block_t block;
  /* How many blocks we have.  */
  int num;
  /* A (page-aligned) buffer pointing to the data we're dealing with.  */
  vm_address_t buf;
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
      int length = pb->num << log2_block_size;

      ext2_debug ("writing block %lu[%d]", pb->block, pb->num);

      if (pb->offs > 0)
	/* Put what we're going to write into a page-aligned buffer.  */
	{
	  vm_address_t page_buf = get_page_buf ();
	  bcopy ((char *)pb->buf + pb->offs, (void *)page_buf, length);
	  err = dev_write_sync (dev_block, page_buf, length);
	  free_page_buf (page_buf);
	}
      else
	err = dev_write_sync (dev_block, pb->buf, length);
      if (err)
	return err;

      pb->offs += length;
      pb->num = 0;
    }

  return 0;
}

static void
pending_blocks_init (struct pending_blocks *pb, vm_address_t buf)
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

/* ---------------------------------------------------------------- */

/* Write one page for the pager backing NODE, at offset PAGE, into BUF.  This
   may need to write several filesystem blocks to satisfy one page, and tries
   to consolidate the i/o if possible.  */
static error_t
file_pager_write_page (struct node *node, vm_offset_t offset, vm_address_t buf)
{
  error_t err = 0;
  struct pending_blocks pb;
  struct rwlock *lock = 0;
  block_t block;
  int left = vm_page_size;

  pending_blocks_init (&pb, buf);

  if (offset + left > node->allocsize)
    left = node->allocsize - offset;

  ext2_debug ("writing inode %d page %d[%d]", node->dn->number, offset, left);

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
      
  if (lock)
    rwlock_reader_unlock (lock);

  return err;
}

/* ---------------------------------------------------------------- */

static error_t
disk_pager_read_page (vm_offset_t page, vm_address_t *buf, int *writelock)
{
  error_t err;
  int length = vm_page_size;

  if (page + vm_page_size > device_size)
    length = device_size - page;

  err = dev_read_sync (page / device_block_size, (void *)buf, length);
  if (!err && length != vm_page_size)
    bzero ((void *)(*buf + length), vm_page_size - length);

  *writelock = 0;

  return err;
}

static error_t
disk_pager_write_page (vm_offset_t page, vm_address_t buf)
{
  error_t err = 0;
  int length = vm_page_size;

  if (page + vm_page_size > device_size)
    length = device_size - page;

  ext2_debug ("writing disk page %d[%d]", page, length);

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
    err = dev_write_sync (page / device_block_size, buf, length);

  return err;
}

/* ---------------------------------------------------------------- */

/* Satisfy a pager read request for either the disk pager or file pager
   PAGER, to the page at offset PAGE into BUF.  WRITELOCK should be set if
   the pager should make the page writeable.  */
error_t
pager_read_page (struct user_pager_info *pager, vm_offset_t page,
		      vm_address_t *buf, int *writelock)
{
  if (pager->type == DISK)
    return disk_pager_read_page (page, buf, writelock);
  else
    return file_pager_read_page (pager->node, page, buf, writelock);
}

/* Satisfy a pager write request for either the disk pager or file pager
   PAGER, from the page at offset PAGE from BUF.  */
error_t
pager_write_page (struct user_pager_info *pager, vm_offset_t page,
		  vm_address_t buf)
{
  if (pager->type == DISK)
    return disk_pager_write_page (page, buf);
  else
    return file_pager_write_page (pager->node, page, buf);
}

/* ---------------------------------------------------------------- */

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
      volatile block_t block = page >> log2_block_size;
      struct node *node = pager->node;
      struct disknode *dn = node->dn;

      rwlock_writer_lock (&dn->alloc_lock);
      err = diskfs_catch_exception ();

      if (!err)
	{
	  int left = vm_page_size;

	  if (page + left > node->allocsize)
	    /* Only actually create blocks up to allocsize; diskfs_grow will
	       allocate the rest if called.  */
	    {
	      left = node->allocsize - page;
	      dn->last_page_partially_writable = 1;
	    }

	  while (left > 0)
	    {
	      block_t disk_block;
	      err = ext2_getblk(node, block++, 1, &disk_block);
	      if (err)
		break;
	      left -= block_size;
	    }

	  if (page + vm_page_size >= node->allocsize)
	    dn->last_block_allocated = 1;

#ifdef EXT2FS_DEBUG
	  if (dn->last_page_partially_writable)
	    ext2_debug ("made page %u[%lu] in inode %d partially writable",
			page, node->allocsize - page, dn->number);
	  else
	    ext2_debug ("made page %u[%u] in inode %d writable",
			page, vm_page_size, dn->number);
#endif
	}

      diskfs_end_catch_exception ();
      rwlock_writer_unlock (&dn->alloc_lock);

      if (err == ENOSPC)
	ext2_warning ("This filesystem is out of space, and will now crash.  Bye!");

      return err;
    }
}

/* ---------------------------------------------------------------- */

/* Grow the disk allocated to locked node NODE to be at least SIZE bytes, and
   set NODE->allocsize to the actual allocated size.  (If the allocated size
   is already SIZE bytes, do nothing.)  CRED identifies the user responsible
   for the call.  */
error_t
diskfs_grow (struct node *node, off_t size, struct protid *cred)
{
  assert (!diskfs_readonly);

  if (size > node->allocsize)
    {
      error_t err;
      struct disknode *dn = node->dn;
      vm_offset_t old_size = node->allocsize;
      vm_offset_t new_size = round_block (size);

      ext2_debug ("growing inode %d to %u bytes (from %u)", dn->number,
		  new_size, old_size);

      rwlock_writer_lock (&dn->alloc_lock);
      err = diskfs_catch_exception ();

      if (!err)
	{
	  if (dn->last_page_partially_writable)
	    /* pager_unlock_page has been called on the last page of the
	       file, but only part of the page was created, as the rest went
	       past the end of the file.  As a result, we have to create the
	       rest of the page to preserve the fact that blocks are only
	       created by explicitly making them writable.  */
	    {
	      block_t block = old_size >> log2_block_size;
	      int count = trunc_page (old_size) + vm_page_size - old_size;

	      if (old_size + count < new_size)
		/* The page we're unlocking (and therefore creating) doesn't
		   extend all the way to the end of the file, so there's some
		   unallocated space left there.  */
		dn->last_block_allocated = 0;

	      if (old_size + count > new_size)
		/* This growth won't create the whole of the last page.  */
		count = new_size - old_size;
	      else
		/* This will take care the whole page.  */
		dn->last_page_partially_writable = 0;

	      ext2_debug ("extending writable page %u by %d bytes"
			  "; first new block = %lu",
			  trunc_page (old_size), count, block);

	      while (count > 0)
		{
		  block_t disk_block;
		  err = ext2_getblk(node, block++, 1, &disk_block);
		  if (err)
		    break;
		  count -= block_size;
		}

	      ext2_debug ("new state: page %s, last block %sallocated",
			  dn->last_page_partially_writable
			    ? "still partial" : "completely allocated",
			  dn->last_block_allocated ? "" : "now un");
	    }

	  node->allocsize = new_size;
	}

      diskfs_end_catch_exception ();
      rwlock_writer_unlock (&dn->alloc_lock);

      return err;
    }
  else
    return 0;
}

/* ---------------------------------------------------------------- */

/* This syncs a single file (NODE) to disk.  Wait for all I/O to complete
   if WAIT is set.  NODE->lock must be held.  */
void
diskfs_file_update (struct node *node, int wait)
{
  struct user_pager_info *upi;

  if (!node->dn->last_block_allocated)
    /* Allocate the last block in the file to maintain consistency with the
       file size.  */
    {
      rwlock_writer_lock (&node->dn->alloc_lock);
      if (!node->dn->last_block_allocated) /* check again with the lock */
	{
	  block_t disk_block;
	  block_t block = (node->allocsize >> log2_block_size) - 1;
	  ext2_debug ("allocating final block %lu", block);
	  if (ext2_getblk (node,  block, 1, &disk_block) == 0)
	    node->dn->last_block_allocated = 1;
	}
      rwlock_writer_unlock (&node->dn->alloc_lock);
    }

  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (upi)
    {
      pager_sync (upi->p, wait);
      ports_port_deref (upi->p);
    }
  
  pokel_sync (&node->dn->indir_pokel, wait);

  diskfs_node_update (node, wait);
}

/* ---------------------------------------------------------------- */

/* Return in *OFFSET and *SIZE the minimum valid address the pager will
   accept and the size of the object.  */
inline error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset, vm_size_t *size)
{
  assert (pager->type == DISK || pager->type == FILE_DATA);

  *offset = 0;

  if (pager->type == DISK)
    *size = device_size;
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
      spin_lock (&node_to_page_lock);
      if (upi->node->dn->fileinfo == upi)
	upi->node->dn->fileinfo = 0;
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

/* ---------------------------------------------------------------- */

/* A top-level function for the paging thread that just services paging
   requests.  */
static void
service_paging_requests (any_t foo __attribute__ ((unused)))
{
  for (;;)
    ports_manage_port_operations_multithread (pager_bucket, pager_demuxer,
					      1000 * 60 * 2, 1000 * 60 * 10,
					      1, MACH_PORT_NULL);
}

/* Create a the DISK pager, initializing DISKPAGER, and DISKPAGERPORT */
void
create_disk_pager ()
{
  pager_bucket = ports_create_bucket ();

  /* Make a thread to service paging requests.  */
  cthread_detach (cthread_fork ((cthread_fn_t)service_paging_requests,
				(any_t)0));

  disk_pager = malloc (sizeof (struct user_pager_info));
  disk_pager->type = DISK;
  disk_pager->node = 0;
  disk_pager->p =
    pager_create (disk_pager, pager_bucket,
		  MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  disk_pager_port = pager_get_port (disk_pager->p);
  mach_port_insert_right (mach_task_self (), disk_pager_port, disk_pager_port,
			  MACH_MSG_TYPE_MAKE_SEND);
}  

/* Call this to create a FILE_DATA pager and return a send right.
   NODE must be locked.  */
mach_port_t
diskfs_get_filemap (struct node *node)
{
  struct user_pager_info *upi;
  mach_port_t right;

  assert (S_ISDIR (node->dn_stat.st_mode)
	  || S_ISREG (node->dn_stat.st_mode)
	  || (S_ISLNK (node->dn_stat.st_mode)));

  spin_lock (&node_to_page_lock);
  do
    if (!node->dn->fileinfo)
      {
	upi = malloc (sizeof (struct user_pager_info));
	upi->type = FILE_DATA;
	upi->node = node;
	diskfs_nref_light (node);
	upi->p =
	  pager_create (upi, pager_bucket, MAY_CACHE,
			MEMORY_OBJECT_COPY_DELAY);
	node->dn->fileinfo = upi;
	right = pager_get_port (node->dn->fileinfo->p);
	ports_port_deref (node->dn->fileinfo->p);
      }
    else
      {
	/* Because NP->dn->fileinfo->p is not a real reference,
	   this might be nearly deallocated.  If that's so, then
	   the port right will be null.  In that case, clear here
	   and loop.  The deallocation will complete separately. */
	right = pager_get_port (node->dn->fileinfo->p);
	if (right == MACH_PORT_NULL)
	  node->dn->fileinfo = 0;
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
  struct user_pager_info *upi;
  
  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node_to_page_lock);

  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 0, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    ports_port_deref (upi->p);
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *node)
{
  struct user_pager_info *upi;
  
  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 1, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    ports_port_deref (upi->p);
}

/* Call this to find out the struct pager * corresponding to the
   FILE_DATA pager of inode IP.  This should be used *only* as a subsequent
   argument to register_memory_fault_area, and will be deleted when 
   the kernel interface is fixed.  NODE must be locked.  */
struct pager *
diskfs_get_filemap_pager_struct (struct node *node)
{
  /* This is safe because fileinfo can't be cleared; there must be
     an active mapping for this to be called. */
  return node->dn->fileinfo->p;
}

static struct ext2_super_block final_sblock;

/* Shutdown all the pagers. */
void
diskfs_shutdown_pager ()
{
  error_t shutdown_one (void *v_p)
    {
      struct pager *p = v_p;
      if (p != disk_pager->p)
	pager_shutdown (p);
      return 0;
    }

  write_all_disknodes ();
  
  /* Because the superblock lives in the disk pager, we copy out the last
     known value just before we shut it down.  */
  bcopy (sblock, &final_sblock, sizeof (final_sblock));
  sblock = &final_sblock;

  ports_bucket_iterate (pager_bucket, shutdown_one);
  pager_shutdown (disk_pager->p);
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  error_t sync_one (void *v_p)
    {
      struct pager *p = v_p;
      if (p != disk_pager->p)
	pager_sync (p, wait);
      return 0;
    }
  
  write_all_disknodes ();
  ports_bucket_iterate (pager_bucket, sync_one);

  /* Do things on the the disk pager.  */
  pokel_sync (&global_pokel, wait);
}

/* ---------------------------------------------------------------- */

/* Tell diskfs if there are pagers exported, and if none, then
   prevent any new ones from showing up.  */
int
diskfs_pager_users ()
{
  int npagers;
  
  error_t block_cache (void *arg)
    {
      struct pager *p = arg;
      
      pager_change_attributes (p, 0, MEMORY_OBJECT_COPY_DELAY, 1);
      return 0;
    }
  
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

  npagers = ports_count_bucket (pager_bucket);
  if (npagers <= 1)
    return 0;

  if (MAY_CACHE == 0)
    {
      ports_enable_bucket (pager_bucket);
      return 1;
    }
  
  /* Loop through the pagers and turn off caching one by one,
     synchronously.  That should cause termination of each pager. */
  ports_bucket_iterate (pager_bucket, block_cache);

  /* Give it a second; the kernel doesn't actually shutdown
     immediately.  XXX */
  sleep (1);
  
  npagers = ports_count_bucket (pager_bucket);
  if (npagers <= 1)
    return 0;
  
  /* Darn, there are actual honest users.  Turn caching back on,
     and return failure. */
  ports_bucket_iterate (pager_bucket, enable_cache);
  return 1;
}
