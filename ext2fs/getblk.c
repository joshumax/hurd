/* File block to disk block mapping routines.

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
 *  linux/fs/ext2/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

#include <string.h>
#include "ext2fs.h"

/* 
 * ext2_discard_prealloc and ext2_alloc_block are atomic wrt. the
 * superblock in the same manner as are ext2_free_blocks and
 * ext2_new_block.  We just wait on the super rather than locking it
 * here, since ext2_new_block will do the necessary locking and we
 * can't block until then.
 */
void 
ext2_discard_prealloc (struct node *node)
{
#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count)
    {
      int i = node->dn->info.i_prealloc_count;
      node->dn->info.i_prealloc_count = 0;
      ext2_free_blocks (node->dn->info.i_prealloc_block, i);
    }
#endif
}

static int 
ext2_alloc_block (struct node *node, unsigned long goal)
{
#ifdef EXT2FS_DEBUG
  static unsigned long alloc_hits = 0, alloc_attempts = 0;
#endif
  unsigned long result;
  char *bh;

#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count &&
      (goal == node->dn->info.i_prealloc_block ||
       goal + 1 == node->dn->info.i_prealloc_block))
    {
      result = node->dn->info.i_prealloc_block++;
      node->dn->info.i_prealloc_count--;
      ext2_debug ("preallocation hit (%lu/%lu).\n",
		  ++alloc_hits, ++alloc_attempts);

      bh = bptr (result);
      memset (bh, 0, block_size);
    }
  else
    {
      ext2_discard_prealloc (node);
      ext2_debug ("preallocation miss (%lu/%lu).\n",
		  alloc_hits, ++alloc_attempts);
      if (S_ISREG (node->dn_stat.st_mode))
	result = ext2_new_block
	  (goal,
	   &node->dn->info.i_prealloc_count,
	   &node->dn->info.i_prealloc_block);
      else
	result = ext2_new_block (goal, 0, 0);
    }
#else
  result = ext2_new_block (goal, 0, 0);
#endif

  return result;
}

static error_t
inode_getblk (struct node *node, int nr, int create, int new_block, char **buf)
{
  u32 block;
  int goal = 0, i;
  int blocks = block_size / 512;

  block = node->dn->info.i_data[nr];
  if (block)
    {
      *buf = bptr (block);
      return 0;
    }

  if (!create)
    return EINVAL;

  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;

  ext2_debug ("hint = %d,", goal);

  if (!goal)
    {
      for (i = nr - 1; i >= 0; i--)
	{
	  if (node->dn->info.i_data[i])
	    {
	      goal = node->dn->info.i_data[i];
	      break;
	    }
	}
      if (!goal)
	goal =
	  (node->dn->info.i_block_group * EXT2_BLOCKS_PER_GROUP (sblock))
	  + sblock->s_first_data_block;
    }

  ext2_debug ("goal = %d.\n", goal);

  block = ext2_alloc_block (node, goal);
  if (!block)
    return EIO;

  *buf = bptr (block);
  node->dn->info.i_data[nr] = block;

  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = block;
  node->dn_set_ctime = 1;
  node->dn_stat.st_blocks += blocks << log2_stat_blocks_per_fs_block;
  node->dn_stat_dirty = 1;

  if (diskfs_synchronous || node->dn->info.i_osync)
    diskfs_node_update (node, 1);

  return 0;
}

error_t
block_getblk (struct node *node,
	      char *bh, int nr,
	      int create, int blocksize, int new_block,
	      char **buf)
{
  int i, goal = 0;
  u32 block;
  int blocks = block_size / 512;

  block = ((u32 *)bh)[nr];
  if (block)
    {
      *buf = bptr (block);
      return 0;
    }

  if (!create)
    return EINVAL;

  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;
  if (!goal)
    {
      for (i = nr - 1; i >= 0; i--)
	{
	  if (((u32 *) bh)[i])
	    {
	      goal = ((u32 *) bh)[i];
	      break;
	    }
	}
      if (!goal)
	goal = bptr_block (bh);
    }

  block = ext2_alloc_block (node, goal);
  if (!block)
    return EIO;			/* XXX? */

  *buf = bptr (block);
  ((u32 *)bh)[nr] = block;

  if (diskfs_synchronous || node->dn->info.i_osync)
    sync_global_ptr (bh, 1);
  else
    record_indir_poke (node, bh);

  node->dn_set_ctime = 1;
  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = block;
  node->dn_stat.st_blocks += blocks << log2_stat_blocks_per_fs_block;
  node->dn_stat_dirty = 1;

  return 0;
}

/* Returns in BUF a pointer to the file block BLOCK in NODE.  If there is no
   such block yet, but CREATE is true, then it is created, otherwise EINVAL
   is returned.  */
error_t
ext2_getblk (struct node *node, long block, int create, char **buf)
{
  error_t err;
  char *bh;
  unsigned long b;
  unsigned long addr_per_block = EXT2_ADDR_PER_BLOCK (sblock);

  if (block < 0)
    {
      ext2_warning ("ext2_getblk", "block < 0");
      return EIO;
    }
  if (block > EXT2_NDIR_BLOCKS + addr_per_block +
      addr_per_block * addr_per_block +
      addr_per_block * addr_per_block * addr_per_block)
    {
      ext2_warning ("ext2_getblk", "block > big");
      return EIO;
    }
  /*
     * If this is a sequential block allocation, set the next_alloc_block
     * to this block now so that all the indblock and data block
     * allocations use the same goal zone
   */

  ext2_debug ("block %lu, next %lu, goal %lu.\n", block,
	      node->dn->info.i_next_alloc_block,
	      node->dn->info.i_next_alloc_goal);

  if (block == node->dn->info.i_next_alloc_block + 1)
    {
      node->dn->info.i_next_alloc_block++;
      node->dn->info.i_next_alloc_goal++;
    }

  b = block;

  if (block < EXT2_NDIR_BLOCKS)
    return inode_getblk (node, block, create, b, buf);

  block -= EXT2_NDIR_BLOCKS;
  if (block < addr_per_block)
    {
      err = inode_getblk (node, EXT2_IND_BLOCK, create, b, &bh);
      if (!err)
	err= block_getblk (node, bh, block, create, block_size, b, buf);
      return err;
    }

  block -= addr_per_block;
  if (block < addr_per_block * addr_per_block)
    {
      err = inode_getblk (node, EXT2_DIND_BLOCK, create, b, &bh);
      if (!err)
	err = block_getblk (node, bh, block / addr_per_block, create,
			    block_size, b, &bh);
      if (!err)
	err = block_getblk (node, bh, block & (addr_per_block - 1),
			    create, block_size, b, buf);
      return err;
    }

  block -= addr_per_block * addr_per_block;
  err = inode_getblk (node, EXT2_TIND_BLOCK, create, b, &bh);
  if (!err)
    err = block_getblk (node, bh, block / (addr_per_block * addr_per_block),
			create, block_size, b, &bh);
  if (!err)
    err =
      block_getblk (node, bh, (block / addr_per_block) & (addr_per_block - 1),
		    create, block_size, b, &bh);
  if (!err)
    err = block_getblk (node, bh, block & (addr_per_block - 1), create,
			block_size, b, buf);

  return err;
}
