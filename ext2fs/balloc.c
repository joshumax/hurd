/* Block allocation routines.

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
 *  linux/fs/ext2/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

/*
 * The free blocks are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext2_read_super).
 */

#include <string.h>
#include "ext2fs.h"

#define in_range(b, first, len) ((b) >= (first) && (b) <= (first) + (len) - 1)

void 
ext2_free_blocks (unsigned long block, unsigned long count)
{
  char *bh;
  unsigned long block_group;
  unsigned long bit;
  unsigned long i;
  struct ext2_group_desc *gdp;

  spin_lock (&global_lock);

  if (block < sblock->s_first_data_block ||
      (block + count) > sblock->s_blocks_count)
    {
      ext2_error ("ext2_free_blocks",
		  "Freeing blocks not in datazone - "
		  "block = %lu, count = %lu", block, count);
      spin_unlock (&global_lock);
      return;
    }

  ext2_debug ("freeing block %lu\n", block);

  block_group = (block - sblock->s_first_data_block) /
    sblock->s_blocks_per_group;
  bit = (block - sblock->s_first_data_block) % sblock->s_blocks_per_group;
  if (bit + count > sblock->s_blocks_per_group)
    ext2_panic ("ext2_free_blocks",
		"Freeing blocks across group boundary - "
		"Block = %lu, count = %lu",
		block, count);
  gdp = group_desc (block_group);
  bh = bptr (gdp->bg_block_bitmap);

  if (check_strict &&
      (in_range (gdp->bg_block_bitmap, block, count) ||
       in_range (gdp->bg_inode_bitmap, block, count) ||
       in_range (block, gdp->bg_inode_table, itb_per_group) ||
       in_range (block + count - 1, gdp->bg_inode_table, itb_per_group)))
    ext2_panic ("ext2_free_blocks",
		"Freeing blocks in system zones - "
		"Block = %lu, count = %lu",
		block, count);

  for (i = 0; i < count; i++)
    {
      if (!clear_bit (bit + i, bh))
	ext2_warning ("ext2_free_blocks",
		      "bit already cleared for block %lu", block);
      else
	{
	  gdp->bg_free_blocks_count++;
	  sblock->s_free_blocks_count++;
	}
    }

  pokel_add (&global_pokel, bh, block_size);
  pokel_add (&global_pokel, gdp, sizeof *gdp);

  sblock_dirty = 1;
  spin_unlock (&global_lock);

  alloc_sync (0);
}

/*
 * ext2_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within 
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 */
int 
ext2_new_block (unsigned long goal,
		u32 * prealloc_count, u32 * prealloc_block)
{
  char *bh;
  char *p, *r;
  int i, j, k, tmp;
  unsigned long lmap;
  struct ext2_group_desc *gdp;

#ifdef EXT2FS_DEBUG
  static int goal_hits = 0, goal_attempts = 0;
#endif

  spin_lock (&global_lock);

#ifdef XXX /* Auth check to use reserved blocks  */
  if (sblock->s_free_blocks_count <= sblock->s_r_blocks_count &&
      (!fsuser () && (sb->u.ext2_sb.s_resuid != current->fsuid) &&
       (sb->u.ext2_sb.s_resgid == 0 ||
	!in_group_p (sb->u.ext2_sb.s_resgid))))
    {
      spin_unlock (&global_lock);
      return 0;
    }
#endif

  ext2_debug ("goal=%lu.\n", goal);

repeat:
  /*
     * First, test whether the goal block is free.
   */
  if (goal < sblock->s_first_data_block || goal >= sblock->s_blocks_count)
    goal = sblock->s_first_data_block;
  i = (goal - sblock->s_first_data_block) / sblock->s_blocks_per_group;
  gdp = group_desc (i);
  if (gdp->bg_free_blocks_count > 0)
    {
      j = ((goal - sblock->s_first_data_block) % sblock->s_blocks_per_group);
#ifdef EXT2FS_DEBUG
      if (j)
	goal_attempts++;
#endif
      bh = bptr (gdp->bg_block_bitmap);

      ext2_debug ("goal is at %d:%d.\n", i, j);

      if (!test_bit (j, bh))
	{
#ifdef EXT2FS_DEBUG
	  goal_hits++;
	  ext2_debug ("goal bit allocated.\n");
#endif
	  goto got_block;
	}
      if (j)
	{
	  /*
	     * The goal was occupied; search forward for a free 
	     * block within the next 32 blocks
	   */
	  lmap = ((((unsigned long *) bh)[j >> 5]) >>
		  ((j & 31) + 1));
	  if (j < sblock->s_blocks_per_group - 32)
	    lmap |= (((unsigned long *) bh)[(j >> 5) + 1]) <<
	      (31 - (j & 31));
	  else
	    lmap |= 0xffffffff << (31 - (j & 31));
	  if (lmap != 0xffffffffl)
	    {
	      k = ffz (lmap) + 1;
	      if ((j + k) < sblock->s_blocks_per_group)
		{
		  j += k;
		  goto got_block;
		}
	    }
	}

      ext2_debug ("Bit not found near goal\n");

      /*
       * There has been no free block found in the near vicinity
       * of the goal: do a search forward through the block groups,
       * searching in each group first for an entire free byte in
       * the bitmap and then for any free bit.
       * 
       * Search first in the remainder of the current group; then,
       * cyclicly search through the rest of the groups.
       */
      p = ((char *) bh) + (j >> 3);
      r = memscan (p, 0, (sblock->s_blocks_per_group - j + 7) >> 3);
      k = (r - ((char *) bh)) << 3;
      if (k < sblock->s_blocks_per_group)
	{
	  j = k;
	  goto search_back;
	}
      k = find_next_zero_bit ((unsigned long *) bh,
			      sblock->s_blocks_per_group,
			      j);
      if (k < sblock->s_blocks_per_group)
	{
	  j = k;
	  goto got_block;
	}
    }

  ext2_debug ("Bit not found in block group %d.\n", i);

  /*
     * Now search the rest of the groups.  We assume that 
     * i and gdp correctly point to the last group visited.
   */
  for (k = 0; k < groups_count; k++)
    {
      i++;
      if (i >= groups_count)
	i = 0;
      gdp = group_desc (i);
      if (gdp->bg_free_blocks_count > 0)
	break;
    }
  if (k >= groups_count)
    {
      spin_unlock (&global_lock);
      return 0;
    }
  bh = bptr (gdp->bg_block_bitmap);
  r = memscan (bh, 0, sblock->s_blocks_per_group >> 3);
  j = (r - bh) << 3;
  if (j < sblock->s_blocks_per_group)
    goto search_back;
  else
    j = find_first_zero_bit ((unsigned long *) bh,
			     sblock->s_blocks_per_group);
  if (j >= sblock->s_blocks_per_group)
    {
      ext2_error ("ext2_new_block",
		  "Free blocks count corrupted for block group %d", i);
      spin_unlock (&global_lock);
      return 0;
    }

search_back:
  /* 
     * We have succeeded in finding a free byte in the block
     * bitmap.  Now search backwards up to 7 bits to find the
     * start of this group of free blocks.
   */
  for (k = 0; k < 7 && j > 0 && !test_bit (j - 1, bh); k++, j--);

got_block:

  ext2_debug ("using block group %d(%d)\n", i, gdp->bg_free_blocks_count);

  tmp = j + i * sblock->s_blocks_per_group + sblock->s_first_data_block;

  if (check_strict &&
      (tmp == gdp->bg_block_bitmap ||
       tmp == gdp->bg_inode_bitmap ||
       in_range (tmp, gdp->bg_inode_table, itb_per_group)))
    ext2_panic ("ext2_new_block",
		"Allocating block in system zone; block = %u", tmp);

  if (set_bit (j, bh))
    {
      ext2_warning ("ext2_new_block", "bit already set for block %d", j);
      goto repeat;
    }

  ext2_debug ("found bit %d\n", j);

  /*
     * Do block preallocation now if required.
   */
#ifdef EXT2_PREALLOCATE
  if (prealloc_block)
    {
      *prealloc_count = 0;
      *prealloc_block = tmp + 1;
      for (k = 1;
	   k < 8 && (j + k) < sblock->s_blocks_per_group; k++)
	{
	  if (set_bit (j + k, bh))
	    break;
	  (*prealloc_count)++;
	}
      gdp->bg_free_blocks_count -= *prealloc_count;
      sblock->s_free_blocks_count -= *prealloc_count;
      ext2_debug ("Preallocated a further %lu bits.\n",
		  *prealloc_count);
    }
#endif

  j = tmp;

  pokel_add (&global_pokel, bh, block_size);

  if (j >= sblock->s_blocks_count)
    {
      ext2_error ("ext2_new_block",
		  "block >= blocks count - "
		  "block_group = %d, block=%d", i, j);
      j = 0;
      goto sync_out;
    }

  bh = bptr (j);
  memset (bh, 0, block_size);
  pokel_add (&global_pokel, bh, block_size);

  ext2_debug ("allocating block %d. "
	      "Goal hits %d of %d.\n", j, goal_hits, goal_attempts);

  gdp->bg_free_blocks_count--;
  pokel_add (&global_pokel, gdp, sizeof *gdp);

  sblock->s_free_blocks_count--;
  sblock_dirty = 1;

 sync_out:
  spin_unlock (&global_lock);
  alloc_sync (0);

  return j;
}

unsigned long 
ext2_count_free_blocks ()
{
#ifdef EXT2FS_DEBUG
  unsigned long desc_count, bitmap_count, x;
  struct ext2_group_desc *gdp;
  int i;

  spin_lock (&global_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < groups_count; i++)
    {
      gdp = group_desc (i);
      desc_count += gdp->bg_free_blocks_count;
      x = count_free (bptr (gdb->bg_block_bitmap), block_size);
      printk ("group %d: stored = %d, counted = %lu\n",
	      i, gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  printk ("ext2_count_free_blocks: stored = %lu, computed = %lu, %lu\n",
	  sblock->s_free_blocks_count, desc_count, bitmap_count);
  spin_unlock (&global_lock);
  return bitmap_count;
#else
  return sblock->s_free_blocks_count;
#endif
}

static inline int 
block_in_use (unsigned long block, unsigned char *map)
{
  return test_bit ((block - sblock->s_first_data_block) %
		   sblock->s_blocks_per_group, map);
}

void 
ext2_check_blocks_bitmap ()
{
  char *bh;
  unsigned long desc_count, bitmap_count, x;
  unsigned long desc_blocks;
  struct ext2_group_desc *gdp;
  int i, j;

  spin_lock (&global_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;

  desc_blocks = (groups_count + desc_per_block - 1) / desc_per_block;

  for (i = 0; i < groups_count; i++)
    {
      gdp = group_desc (i);
      desc_count += gdp->bg_free_blocks_count;
      bh = bptr (gdp->bg_block_bitmap);

      if (!test_bit (0, bh))
	ext2_error ("ext2_check_blocks_bitmap",
		    "Superblock in group %d is marked free", i);

      for (j = 0; j < desc_blocks; j++)
	if (!test_bit (j + 1, bh))
	  ext2_error ("ext2_check_blocks_bitmap",
		      "Descriptor block #%d in group "
		      "%d is marked free", j, i);

      if (!block_in_use (gdp->bg_block_bitmap, bh))
	ext2_error ("ext2_check_blocks_bitmap",
		    "Block bitmap for group %d is marked free", i);

      if (!block_in_use (gdp->bg_inode_bitmap, bh))
	ext2_error ("ext2_check_blocks_bitmap",
		    "Inode bitmap for group %d is marked free", i);

      for (j = 0; j < itb_per_group; j++)
	if (!block_in_use (gdp->bg_inode_table + j, bh))
	  ext2_error ("ext2_check_blocks_bitmap",
		      "Block #%d of the inode table in "
		      "group %d is marked free", j, i);

      x = count_free (bh, block_size);
      if (gdp->bg_free_blocks_count != x)
	ext2_error ("ext2_check_blocks_bitmap",
		    "Wrong free blocks count for group %d, "
		    "stored = %d, counted = %lu", i,
		    gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  if (sblock->s_free_blocks_count != bitmap_count)
    ext2_error ("ext2_check_blocks_bitmap",
		"Wrong free blocks count in super block, "
		"stored = %lu, counted = %lu",
		(unsigned long) sblock->s_free_blocks_count, bitmap_count);
  spin_unlock (&global_lock);
}
