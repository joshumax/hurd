/* File truncation & growth routines.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Converted to work under the hurd by Miles Bader <miles@gnu.ai.mit.edu>

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

/*
 *  linux/fs/ext2/truncate.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/truncate.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include "ext2fs.h"

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

/* ---------------------------------------------------------------- */

/* Write something to each page from START to END inclusive of memory
   object OBJ, but make sure the data doesns't actually change. */
static void
poke_pages (memory_object_t obj,
	    vm_offset_t start,
	    vm_offset_t end)
{
  vm_address_t addr, poke;
  vm_size_t len;
  error_t err;
  
  while (start < end)
    {
      len = 8 * vm_page_size;
      if (len > end - start)
	len = end - start;
      addr = 0;
      err = vm_map (mach_task_self (), &addr, len, 0, 1, obj, start, 0,
		    VM_PROT_WRITE|VM_PROT_READ, VM_PROT_READ|VM_PROT_WRITE, 0);
      if (!err)
	{
	  for (poke = addr; poke < addr + len; poke += vm_page_size)
	    *(volatile int *)poke = *(volatile int *)poke;
	  vm_deallocate (mach_task_self (), addr, len);
	}
      start += len;
    }
}

/* ---------------------------------------------------------------- */

#define DIRECT_BLOCK(length) \
  ((length + block_size - 1) / block_size)
#define INDIRECT_BLOCK(length, offset) \
  ((int)DIRECT_BLOCK(length) - offset)
#define DINDIRECT_BLOCK(length, offset) \
  (((int)DIRECT_BLOCK(length) - offset) / addr_per_block)
#define TINDIRECT_BLOCK(length) \
  (((int)DIRECT_BLOCK(length) \
    - (addr_per_block * addr_per_block + addr_per_block + EXT2_NDIR_BLOCKS)) \
   / (addr_per_block * addr_per_block))

static void
trunc_direct (struct node * node, unsigned long length)
{
  u32 block;
  int i;
  char * bh;
  unsigned long block_to_free = 0;
  unsigned long free_count = 0;
  int blocks = block_size / 512;
  int direct_block = DIRECT_BLOCK(length);

  for (i = direct_block ; i < EXT2_NDIR_BLOCKS ; i++)
    {
      block = node->dn->info.i_data[i];
      if (!block)
	continue;

      bh = bptr(block);

      node->dn->info.i_data[i] = 0;

      node->dn_stat.st_blocks -= blocks << log2_stat_blocks_per_fs_block;
      node->dn_stat_dirty = 1;

      if (free_count == 0) {
	block_to_free = block;
	free_count++;
      } else if (free_count > 0 && block_to_free == block - free_count)
	free_count++;
      else {
	ext2_free_blocks (block_to_free, free_count);
	block_to_free = block;
	free_count = 1;
      }

#if 0
      ext2f_free_blocks (block, 1);
#endif
    }

  if (free_count > 0)
    ext2_free_blocks (block_to_free, free_count);
}

/* ---------------------------------------------------------------- */

static void
trunc_indirect (struct node * node, unsigned long length,
		int offset, u32 * p)
{
  int i, block;
  char * bh;
  char * ind_bh;
  u32 * ind;
  unsigned long block_to_free = 0;
  unsigned long free_count = 0;
  int blocks = block_size / 512;
  int indirect_block = INDIRECT_BLOCK (length, offset);

  block = *p;
  if (!block)
    return;

  ind_bh = bptr (block);
  if (!ind_bh) {
    *p = 0;
    return;
  }

  for (i = indirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;

      ind = (u32 *)ind_bh + i;
      block = *ind;
      if (!block)
	continue;

      bh = bptr (block);

      *ind = 0;
      record_indir_poke (node, ind_bh);

      if (free_count == 0)
	{
	  block_to_free = block;
	  free_count++;
	}
      else if (free_count > 0 && block_to_free == block - free_count)
	free_count++;
      else
	{
	  ext2_free_blocks (block_to_free, free_count);
	  block_to_free = block;
	  free_count = 1;
	}

#if 0
      ext2_free_blocks (block, 1);
#endif

      node->dn_stat.st_blocks -= blocks << log2_stat_blocks_per_fs_block;
      node->dn_stat_dirty = 1;
    }

  if (free_count > 0)
    ext2_free_blocks (block_to_free, free_count);

  ind = (u32 *) ind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(ind++))
      break;
  if (i >= addr_per_block)
    {
      block = *p;
      *p = 0;
      node->dn_stat.st_blocks -= blocks << log2_stat_blocks_per_fs_block;
      node->dn_stat_dirty = 1;
      ext2_free_blocks (block, 1);
    }
}

/* ---------------------------------------------------------------- */

static void
trunc_dindirect (struct node * node, unsigned long length,
		 int offset, u32 * p)
{
  int i, block;
  char * dind_bh;
  u32 * dind;
  int blocks = block_size / 512;
  int dindirect_block = DINDIRECT_BLOCK (length, offset);

  block = *p;
  if (!block)
    return;

  dind_bh = bptr (block);
  if (!dind_bh)
    {
      *p = 0;
      return;
    }

  for (i = dindirect_block ; i < addr_per_block ; i++) {
    if (i < 0)
      i = 0;

    dind = i + (u32 *) dind_bh;
    block = *dind;
    if (!block)
      continue;

    trunc_indirect (node, length, offset + (i * addr_per_block), dind);

    record_indir_poke (node, dind_bh);
  }

  dind = (u32 *) dind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(dind++))
      break;

  if (i >= addr_per_block)
    {
      block = *p;
      *p = 0;
      node->dn_stat.st_blocks -= blocks << log2_stat_blocks_per_fs_block;
      node->dn_stat_dirty = 1;
      ext2_free_blocks (block, 1);
    }
}

/* ---------------------------------------------------------------- */

static void
trunc_tindirect (struct node * node, unsigned long length)
{
  int i, block;
  char * tind_bh;
  u32 * tind, * p;
  int blocks = block_size / 512;
  int tindirect_block = TINDIRECT_BLOCK (length);

  p = node->dn->info.i_data + EXT2_TIND_BLOCK;
  if (!(block = *p))
    return;

  tind_bh = bptr (block);
  if (!tind_bh)
    {
      *p = 0;
      return;
    }

  for (i = tindirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;

      tind = i + (u32 *) tind_bh;
      trunc_dindirect(node, length,
		      (EXT2_NDIR_BLOCKS
		       + addr_per_block
		       + (i + 1) * addr_per_block * addr_per_block),
		      tind);
      record_indir_poke (node, tind_bh);
    }

  tind = (u32 *) tind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(tind++))
      break;

  if (i >= addr_per_block)
    {
      block = *p;
      *p = 0;
      node->dn_stat.st_blocks -= blocks << log2_stat_blocks_per_fs_block;
      node->dn_stat_dirty = 1;
      ext2_free_blocks (block, 1);
    }
}

/* ---------------------------------------------------------------- */

/* Flush all the data past the new size from the kernel.  Also force any
   delayed copies of this data to take place immediately.  (We are implicitly
   changing the data to zeros and doing it without the kernel's immediate
   knowledge; accordingl we must help out the kernel thusly.) */
static void
force_delayed_copies (struct node *node, off_t length)
{
  struct user_pager_info *upi;

  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
 
  if (upi)
    {
      mach_port_t obj;
      
      pager_change_attributes (upi->p, MAY_CACHE, MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (node);
      poke_pages (obj, round_page (length), round_page (node->allocsize));
      mach_port_deallocate (mach_task_self (), obj);
      pager_flush_some (upi->p, round_page(length), node->allocsize - length, 1);
      pager_unreference (upi->p);
    }
}

static void
enable_delayed_copies (struct node *node)
{
  struct user_pager_info *upi;

  spin_lock (&node_to_page_lock);
  upi = node->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);

  spin_unlock (&node_to_page_lock);
  if (upi)
    {
      pager_change_attributes (upi->p, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY, 0);
      pager_unreference (upi->p);
    }
}

/* ---------------------------------------------------------------- */

/* The user must define this function.  Truncate locked node NODE to be SIZE
   bytes long.  (If NODE is already less than or equal to SIZE bytes
   long, do nothing.)  If this is a symlink (and diskfs_shortcut_symlink
   is set) then this should clear the symlink, even if 
   diskfs_create_symlink_hook stores the link target elsewhere.  */
error_t
diskfs_truncate (struct node *node, off_t length)
{
  error_t err;
  int offset;
  mode_t mode = node->dn_stat.st_mode;

  assert (!diskfs_readonly);

  if (S_ISDIR(mode))
    return EISDIR;
  if (!S_ISREG(mode))
    return EINVAL;
  if (IS_APPEND(node) || IS_IMMUTABLE(node))
    return EINVAL;

  if (length >= node->dn_stat.st_size)
    return 0;

  /*
   * If the file is not being truncated to a block boundary, the
   * contents of the partial block following the end of the file must be
   * zeroed in case it ever becomes accessible again because of
   * subsequent file growth.
   */
  offset = length % block_size;
  if (offset > 0)
    {
      diskfs_node_rdwr (node, (void *)zeroblock, length, block_size - offset,
			1, 0, 0);
      diskfs_file_update (node, 1);
    }

  ext2_discard_prealloc(node);

  force_delayed_copies (node, length);

  rwlock_writer_lock (&node->dn->alloc_lock);

  /* Update the size on disk; fsck will finish freeing blocks if necessary
     should we crash. */
  node->dn_stat.st_size = length;
  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  diskfs_node_update (node, 1);

  err = diskfs_catch_exception();
  if (!err)
    {
      trunc_direct(node, length);
      trunc_indirect (node, length, EXT2_IND_BLOCK,
		      (u32 *) &node->dn->info.i_data[EXT2_IND_BLOCK]);
      trunc_dindirect (node, length, EXT2_IND_BLOCK +
		       EXT2_ADDR_PER_BLOCK(sblock),
		       (u32 *) &node->dn->info.i_data[EXT2_DIND_BLOCK]);
      trunc_tindirect (node, length);
    }

  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  node->dn_stat_dirty = 1;

  /* Now we can permit delayed copies again. */
  enable_delayed_copies (node);

  return 0;
}

/* The user must define this function.  Grow the disk allocated to locked node
   NODE to be at least SIZE bytes, and set NODE->allocsize to the actual
   allocated size.  (If the allocated size is already SIZE bytes, do
   nothing.)  CRED identifies the user responsible for the call.  */
error_t
diskfs_grow (struct node *node, off_t size, struct protid *cred)
{
  assert (!diskfs_readonly);
  if (size > node->allocsize)
    node->allocsize = size;
  return 0;
}
