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

/*
 * Truncate has the most races in the whole filesystem: coding it is
 * a pain in the a**. Especially as I don't do any locking...
 *
 * The code may look a bit weird, but that's just because I've tried to
 * handle things like file-size changes in a somewhat graceful manner.
 * Anyway, truncating a file at the same time somebody else writes to it
 * is likely to result in pretty weird behaviour...
 *
 * The new code handles normal truncates (size = 0) as well as the more
 * general case (size = XXX). I hope.
 */

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
      block = node->dn.info.i_data[i];
      if (!block)
	continue;

      bh = block_image(block);

      if (i < direct_block)
	goto repeat;

      node->dn.info.i_data[i] = 0;

      node->dn_stat.st_blocks -= blocks;
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
    return 0;

  ind_bh = block_image (block);
  if (!ind_bh) {
    *p = 0;
    return 0;
  }

  for (i = indirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;

      ind = (u32 *)ind_bh + i;
      block = *ind;
      if (!block)
	continue;

      bh = block_image (block);
      if (i < indirect_block)
	goto repeat;

      *ind = 0;
      pokel_add (&node->dn.pokel, ind, sizeof *ind);

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

      node->dn_stat.st_blocks -= blocks;
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
      node->dn_stat.st_blocks -= blocks;
      node->dn_stat_dirty = 1;
      ext2_free_blocks (block, 1);
    }

  return retry;
}

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
    return 0;

  dind_bh = block_image (block);
  if (!dind_bh)
    {
      *p = 0;
      return 0;
    }

  for (i = dindirect_block ; i < addr_per_block ; i++) {
    if (i < 0)
      i = 0;

    dind = i + (u32 *) dind_bh;
    block = *dind;
    if (!block)
      continue;

    trunc_indirect (node, offset + (i * addr_per_block), dind);

    pokel_add (&node->dn.pokel, dindh_bh, block_size);
  }

  dind = (u32 *) dind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(dind++))
      break;

  if (i >= addr_per_block)
    {
      block = *p;
      *p = 0;
      node->dn_stat.st_blocks -= blocks;
      node->dn_stat_dirty = 1;
      ext2_free_blocks (block, 1);
    }

  return retry;
}

static void
trunc_tindirect (struct node * node, unsigned long length)
{
  int i, block;
  char * tind_bh;
  u32 * tind, * p;
  int retry = 0;
  int blocks = block_size / 512;
  int tindirect_block = TINDIRECT_BLOCK (length);

  p = node->dn.info.i_data + EXT2_TIND_BLOCK;
  if (!(block = *p))
    return 0;

  tind_bh = block_image (block);
  if (!tind_bh)
    {
      *p = 0;
      return 0;
    }

  for (i = tindirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;

      tind = i + (u32 *) tind_bh;
      trunc_dindirect(node,
		      (EXT2_NDIR_BLOCKS
		       + addr_per_block
		       + (i + 1) * addr_per_block * addr_per_block),
		      tind);
      pokel_add (&node->dn.pokel, tindh_bh, block_size);
    }

  tind = (u32 *) tind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(tind++))
      break;

  if (i >= addr_per_block)
    {
      block = *p;
      *p = 0;
      node->dn_stat.st_blocks -= blocks;
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
  spin_lock (&node_to_page_lock);
  upi = np->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
 
  if (upi)
    {
      mach_port_t obj;
      
      pager_change_attributes (upi->p, MAY_CACHE, MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (np);
      poke_pages (obj, round_page (length), round_page (np->allocsize));
      mach_port_deallocate (mach_task_self (), obj);
      pager_flush_some (upi->p, round_page(length), np->allocsize - length, 1);
      pager_unreference (upi->p);
    }
}

static void
enable_delayed_copies (struct node *node)
{
  spin_lock (&node_to_page_lock);
  upi = np->dn->fileinfo;
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

/* The user must define this function.  Truncate locked node NP to be SIZE
   bytes long.  (If NP is already less than or equal to SIZE bytes
   long, do nothing.)  If this is a symlink (and diskfs_shortcut_symlink
   is set) then this should clear the symlink, even if 
   diskfs_create_symlink_hook stores the link target elsewhere.  */
error_t
diskfs_truncate (struct node *np, off_t length)
{
  int offset;
  mode_t mode = node->dn_state.st_mode;

  if (S_ISDIR(mode))
    return EISDIR;
  if (!S_ISREG(mode))
    return EINVAL;
  if (IS_APPEND(node) || IS_IMMUTABLE(node))
    return EINVAL;

  if (length >= np->dn_stat.st_size)
    return 0;

  assert (!diskfs_readonly);

  /*
   * If the file is not being truncated to a block boundary, the
   * contents of the partial block following the end of the file must be
   * zeroed in case it ever becomes accessible again because of
   * subsequent file growth.
   */
  offset = length % block_size;
  if (offset > 0)
    {
      diskfs_node_rdwr (np, (void *)zeroblock, length, block_size - offset,
			1, 0, 0);
      diskfs_file_update (np, 1);
    }

  ext2_discard_prealloc(node);

  force_delayed_copies (np, length);

  rwlock_writer_lock (&np->dn->alloc_lock);

  /* Update the size on disk; fsck will finish freeing blocks if necessary
     should we crash. */
  np->dn_stat.st_size = length;
  np->dn_set_mtime = 1;
  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  err = diskfs_catch_exception();
  if (!err)
    {
      trunc_direct(node, length);
      trunc_indirect (node, length, EXT2_IND_BLOCK,
		      (u32 *) &node->dn.info.i_data[EXT2_IND_BLOCK]);
      trunc_dindirect (node, length, EXT2_IND_BLOCK +
		       EXT2_ADDR_PER_BLOCK(sblock),
		       (u32 *) &node->dn.info.i_data[EXT2_DIND_BLOCK]);
      trunc_tindirect (node, length);
    }

  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  node->dn_stat_dirty = 1;

  /* Now we can permit delayed copies again. */
  enable_delayed_copies (np);
}

/* The user must define this function.  Grow the disk allocated to locked node
   NP to be at least SIZE bytes, and set NP->allocsize to the actual
   allocated size.  (If the allocated size is already SIZE bytes, do
   nothing.)  CRED identifies the user responsible for the call.  */
error_t
diskfs_grow (struct node *np, off_t size, struct protid *cred)
{
  if (size > np->allocsize)
    np->allocsize = size;
  return 0;
}
