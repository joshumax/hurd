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

spin_lock_t pager_list_lock = SPIN_LOCK_INITIALIZER;
struct user_pager_info *file_pager_list;

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
	    daddr_t *block, struct rwlock **lock)
{
  error_t err;
  char *bptr;

  if (!*lock)
    {
      *lock = &node->dn->alloc_lock;
      rwlock_reader_lock (*lock);
    }

  if (offset + block_size > node->allocsize)
    return EIO;

  err = ext2_getblk (node, offset >> log2_block_size, 0, &bptr);
  if (err == EINVAL)
    /* Don't barf yet if the node is unallocated.  */
    {
      *block = 0;
      err = 0;
    }
  else if (err == 0)
    *block = bptr_block (bptr);

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
  daddr_t pending_blocks = 0;
  int num_pending_blocks = 0;

  /* Read the NUM_PENDING_BLOCKS blocks in PENDING_BLOCKS, into the buffer
     pointed to by BUF (allocating it if necessary) at offset OFFS.  OFFS in
     adjusted by the amount read, and NUM_PENDING_BLOCKS is zeroed.  Any read
     error is returned.  */
  error_t do_pending_reads ()
    {
      if (num_pending_blocks > 0)
	{
	  daddr_t dev_block = pending_blocks << log2_dev_blocks_per_fs_block;
	  int length = num_pending_blocks << log2_block_size;
	  vm_address_t new_buf;

	  err = dev_read_sync (dev_block, &new_buf, length);
	  if (err)
	    return err;

	  if (offs == 0)
	    /* First read, make the returned page be our buffer.  */
	    *buf = new_buf;
	  else
	    /* We've already got some buffer, so copy into it.  */
	    bcopy ((char *)*buf + offs, (char *)new_buf, length);

	  offs += length;
	  num_pending_blocks = 0;
	}

      return 0;
    }

  if (page + left > node->allocsize)
    left = node->allocsize - page;

  while (left > 0)
    {
      daddr_t block;

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
  daddr_t block;
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
      daddr_t dev_block = pb->block << log2_dev_blocks_per_fs_block;
      int length = pb->num << log2_block_size;

printf ("Writing block %lu[%d]\n", pb->block, pb->num);

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
pending_blocks_add (struct pending_blocks *pb, daddr_t block)
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
  u32 block;
  int left = vm_page_size;

  pending_blocks_init (&pb, buf);

  if (offset + left > node->allocsize)
    left = left > node->allocsize - offset;

 printf ("Writing file_pager (inode %d) page %d[%d]\n",
	 node->dn->number, offset, left);

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

 printf ("Writing disk_pager page %d[%d]\n", page, length);

  if (modified_global_blocks)
    /* Be picky about which blocks in a page that we write.  */
    {
      vm_offset_t offs = page;
      struct pending_blocks pb;

      pending_blocks_init (&pb, buf);

      while (length > 0 && !err)
	{
	  int modified;
	  daddr_t block = boffs_block (offs);

	  spin_lock (&modified_global_blocks_lock);
	  modified = clear_bit (block, modified_global_blocks);
	  spin_unlock (&modified_global_blocks_lock);

	  if (modified)
	    /* This block's been modified, so write it out.  */
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

/* Make page PAGE writable.  */
error_t
pager_unlock_page (struct user_pager_info *pager, vm_offset_t page)
{
  if (pager->type == DISK)
    return 0;
  else
    {
      error_t err;
      char *buf;
      daddr_t block = page >> log2_block_size;
      struct node *node = pager->node;
      struct disknode *dn = node->dn;

      rwlock_writer_lock (&dn->alloc_lock);
      err = diskfs_catch_exception ();

      if (!err)
	{
	  int left = vm_page_size;
	  while (left > 0)
	    {
	      err = ext2_getblk(node, block++, 1, &buf);
	      if (err)
		break;
	      left -= block_size;
	    }
	}

      diskfs_end_catch_exception ();
      rwlock_writer_unlock (&dn->alloc_lock);

      return err;
    }
}

/* ---------------------------------------------------------------- */

/* The user must define this function.  Grow the disk allocated to locked node
   NODE to be at least SIZE bytes, and set NODE->allocsize to the actual
   allocated size.  (If the allocated size is already SIZE bytes, do
   nothing.)  CRED identifies the user responsible for the call.  */
error_t
diskfs_grow (struct node *node, off_t size, struct protid *cred)
{
  assert (!diskfs_readonly);

  if (size > node->allocsize)
    node->allocsize = trunc_block (size) + block_size;
  else
    return 0;
}

/* Implement the pager_report_extent callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
inline error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  assert (pager->type == DISK || pager->type == FILE_DATA);

  *offset = 0;

  if (pager->type == DISK)
    *size = device_size;
  else
    *size = pager->node->allocsize;
  
  return 0;
}

/* Implement the pager_clear_user_data callback from the pager library.
   See <hurd/pager.h> for the interface description. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  assert (upi->type == FILE_DATA);
  spin_lock (&node_to_page_lock);
  upi->node->dn->fileinfo = 0;
  spin_unlock (&node_to_page_lock);
  diskfs_nrele_light (upi->node);
  *upi->prevp = upi->next;
  if (upi->next)
    upi->next->prevp = upi->prevp;
  free (upi);
}

/* ---------------------------------------------------------------- */

/* Create a the DISK pager, initializing DISKPAGER, and DISKPAGERPORT */
void
create_disk_pager ()
{
  disk_pager = malloc (sizeof (struct user_pager_info));
  disk_pager->type = DISK;
  disk_pager->node = 0;
  disk_pager->p = pager_create (disk_pager, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
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
  if (!node->dn->fileinfo)
    {
      upi = malloc (sizeof (struct user_pager_info));
      upi->type = FILE_DATA;
      upi->node = node;
      diskfs_nref_light (node);
      upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY);
      node->dn->fileinfo = upi;

      spin_lock (&pager_list_lock);
      upi->next = file_pager_list;
      upi->prevp = &file_pager_list;
      if (upi->next)
	upi->next->prevp = &upi->next;
      file_pager_list = upi;
      spin_unlock (&pager_list_lock);
    }
  right = pager_get_port (node->dn->fileinfo->p);
  spin_unlock (&node_to_page_lock);
  
  mach_port_insert_right (mach_task_self (), right, right,
			  MACH_MSG_TYPE_MAKE_SEND);

  return right;
} 

/* This syncs a single file (NODE) to disk.  Wait for all I/O to complete
   if WAIT is set.  NODE->lock must be held.  */
void
diskfs_file_update (struct node *node, int wait)
{
  struct user_pager_info *upi;

  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (upi)
    {
      pager_sync (upi->p, wait);
      pager_unreference (upi->p);
    }
  
  pokel_sync (&node->dn->pokel, wait);

  diskfs_node_update (node, wait);
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
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);

  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 0, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    pager_unreference (upi->p);
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
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 1, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    pager_unreference (upi->p);
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

/* Call function FUNC (which takes one argument, a pager) on each pager, with
   all file pagers being processed before the disk pager.  Make the calls
   while holding no locks. */
static void
pager_traverse (void (*func)(struct user_pager_info *))
{
  struct user_pager_info *p;
  struct item {struct item *next; struct user_pager_info *p;} *list = 0;
  struct item *i;
  
  spin_lock (&pager_list_lock);
  for (p = file_pager_list; p; p = p->next)
    {
      i = alloca (sizeof (struct item));
      i->next = list;
      list = i;
      pager_reference (p->p);
      i->p = p;
    }
  spin_unlock (&pager_list_lock);
  
  for (i = list; i; i = i->next)
    {
      (*func)(i->p);
      pager_unreference (i->p->p);
    }
  
  (*func)(disk_pager);
}

/* Shutdown all the pagers. */
void
diskfs_shutdown_pager ()
{
  void shutdown_one (struct user_pager_info *p)
    {
      pager_shutdown (p->p);
    }

  write_all_disknodes ();
  pager_traverse (shutdown_one);
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  void sync_one (struct user_pager_info *p)
    {
      if (p != disk_pager)
	pager_sync (p->p, wait);
      else
	pokel_sync (&global_pokel, wait);
    }
  
  write_all_disknodes ();
  pager_traverse (sync_one);
}
